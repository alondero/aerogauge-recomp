// Developer save-state (issue #17; ported from the Lamborghini repo's #22): snapshot the
// guest's RAM at a frame boundary and restore it later, so a hand-found moment (e.g. a craft
// parked where a rendering bug reproduces) can be returned to deterministically --
// interactively (F7 save / F8 load) or headless (AERO_STATE_LOAD=<file>) so an autonomous
// debugging agent can re-reach the spot with no human at the wheel.
//
// What is captured: the low 8 MiB of RDRAM (rdram[0 .. 0x800000)), which is the entire
// guest-addressable N64 RAM -- every object table, the camera, the craft transforms, the
// scene-manager words all live here. The extended region above 8 MiB (librecomp's PI handles
// at 0x80800000, the mod heap) is boot-static setup, identical every run, so it is neither
// saved nor restored. A raw memcpy preserves RDRAM's byte-swizzle (MEM_B's ^3), so save and
// restore are symmetric with no un-swizzle.
//
// Why this is a frame-boundary operation, not a true resumable save-state: each guest
// OSThread is a real native std::thread with its CPU register file (recomp_context) on a
// native C stack that cannot be serialised. So we snapshot/restore ONLY RDRAM, and do it at
// the entry of the per-frame scene driver (func_80015C8C, one instruction past the warp
// hook) -- the point where the game is about to read the scene words and rebuild the frame
// from RAM. Restoring RDRAM there and letting the driver run rebuilds the frame from the
// restored world. This works because N64 games keep persistent state in RAM and derive
// transient CPU/RSP state each frame; it is a debug tool, not a play-anywhere quicksave.
//
// A load overwrites the scene words (0x8013FF80..) too, so a snapshot taken mid-race is
// self-contained: loading it even from the title screen drops straight into that race.
//
// USAGE
//   Interactive: F7 saves the current RAM to the slot file, F8 restores it.
//   Headless:    AERO_STATE_SAVE=<file> auto-captures a spot (scene/delay gated);
//                AERO_STATE_LOAD=<file> auto-loads on a cold boot -> lands on the spot.
//   AERO_STATE_FILE overrides the F7/F8 slot path (default aero_savestate.astate).
//
// RELIABILITY / THE ONE RULE: SETTLE BEFORE YOU SAVE.
//   Only guest RAM is captured; each guest thread also has native execution state (registers,
//   native call stack) that CANNOT be snapshotted. The main game thread is safe because this
//   hook pins it at a fixed frame boundary, but a background loader mid-DMA is not -- if a
//   snapshot is taken WHILE course/craft data is still streaming in, restoring RAM under
//   that thread leaves it mid-job with mismatched native state, which crashes or freezes on
//   load (non-deterministically). The tick therefore fires every trigger only from a SETTLED
//   frame (scene == request, stable 30+ ticks, race phase 3 -- see the guard in the tick);
//   on top of that, still prefer to save from a quiet spot. The same limit means the native
//   RT64 / VI / audio threads keep reading RDRAM during the copy; a settled restore has been
//   stable in the Lamborghini port, a mid-scene load can momentarily race those readers.
//
// Threading mirrors src/aero_warp.c: the SDL main thread only flips an atomic request bit
// (F7/F8 edge-detected in main.cpp's input_sample); the actual RAM copy happens on the game
// thread inside aero_savestate_tick, at the frame-boundary hook.

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

// After a wholesale RDRAM restore, guest RAM's OSThread.context fields hold the SAVE
// process's native pointers (dangling here). ultramodern re-points them to this process's
// live thread contexts (patches/0007). This is sound only for OSThreads the loading process
// already has: the game's threads are all created during boot, so a cold-boot load covers
// them, but any thread the snapshot captured that this process lacks keeps a dangling
// context and the scheduler would fault on it -- another reason to snapshot settled scenes.
extern void ultramodern_relink_thread_contexts(uint8_t* rdram);

#define SCENE_CUR   0x8013FF80u       // u32 current scene (see src/aero_warp.c; race = 5)
#define SCENE_REQ   0x8013FF84u       // u32 requested scene (cur != req => transition in flight)
#define SCENE_PHASE 0x8013FF88u       // u32 scene-local phase; race walks 1->2 (loading) -> 3 (running)
#define RDRAM_SNAP_SIZE 0x800000u     // low 8 MiB = guest-addressable N64 RAM

// On-disk header (32 bytes). Written/read host-endian: a save-state is a local debug
// artifact, not a portable format. Magic+version+size are validated before any RAM is
// touched, so a truncated or foreign file is rejected rather than corrupting the game.
#define STATE_MAGIC "AEROSTAT"
#define STATE_VERSION 1u
typedef struct {
    char     magic[8];      // "AEROSTAT"
    uint32_t version;       // STATE_VERSION
    uint32_t rdram_size;    // RDRAM_SNAP_SIZE
    uint32_t scene;         // captured scene word (informational)
    uint32_t reserved[3];
} state_header_t;

// Default file for the F7/F8 slot; AERO_STATE_FILE overrides. AERO_STATE_LOAD names a
// one-shot boot load (headless agent entry) and is parsed on the first tick.
#define DEFAULT_STATE_PATH "aero_savestate.astate"

// Request bits flipped by the SDL thread, consumed on the game thread.
#define REQ_SAVE 0x1u
#define REQ_LOAD 0x2u
static _Atomic uint32_t g_req;

static const char* slot_path(void) {
    const char* p = getenv("AERO_STATE_FILE");
    return (p != NULL && p[0] != '\0') ? p : DEFAULT_STATE_PATH;
}

// Snapshot rdram[0..8MiB) to <path> via a temp file + rename, so a crash mid-write can
// never leave a torn state file.
static void do_save(uint8_t* rdram, const char* path) {
    char tmp[1100];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp)) {
        fprintf(stderr, "[savestate] save: path too long: %s\n", path);
        return;
    }
    FILE* f = fopen(tmp, "wb");
    if (f == NULL) {
        fprintf(stderr, "[savestate] save: cannot open %s for write\n", tmp);
        return;
    }
    state_header_t h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, STATE_MAGIC, 8);
    h.version    = STATE_VERSION;
    h.rdram_size = RDRAM_SNAP_SIZE;
    h.scene      = (uint32_t)MEM_W(0, (gpr)(int32_t)SCENE_CUR);
    int ok = (fwrite(&h, 1, sizeof(h), f) == sizeof(h)) &&
             (fwrite(rdram, 1, RDRAM_SNAP_SIZE, f) == RDRAM_SNAP_SIZE);
    // Flush before rename so the rename publishes a fully-written file.
    ok = ok && (fflush(f) == 0);
    fclose(f);
    if (!ok) {
        fprintf(stderr, "[savestate] save: write failed for %s\n", path);
        remove(tmp);
        return;
    }
    // Publish atomically. POSIX rename() replaces the target in place; Windows rename()
    // fails if the target exists, so only there do we remove-then-retry. Crucially we do NOT
    // remove the existing good save until the first rename has failed, and on final failure
    // we KEEP the .tmp (it holds the full snapshot) -- so a previously-working save is never
    // lost with nothing to show for it.
    if (rename(tmp, path) != 0) {
        remove(path);
        if (rename(tmp, path) != 0) {
            fprintf(stderr, "[savestate] save: could not publish %s; snapshot kept at %s\n", path, tmp);
            return;
        }
    }
    fprintf(stderr, "[savestate] saved %u bytes to %s (scene=%u)\n",
            RDRAM_SNAP_SIZE, path, h.scene);
}

// Load <path> fully into a scratch buffer and validate BEFORE overwriting any live RAM, so
// a bad/short file aborts the load with the game untouched. Only on full success do we
// memcpy the payload over rdram[0..8MiB).
static void do_load(uint8_t* rdram, const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "[savestate] load: cannot open %s\n", path);
        return;
    }
    state_header_t h;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h) ||
        memcmp(h.magic, STATE_MAGIC, 8) != 0) {
        fprintf(stderr, "[savestate] load: %s is not a save-state (bad magic)\n", path);
        fclose(f);
        return;
    }
    if (h.version != STATE_VERSION || h.rdram_size != RDRAM_SNAP_SIZE) {
        fprintf(stderr, "[savestate] load: %s version/size mismatch (v%u size %u)\n",
                path, h.version, h.rdram_size);
        fclose(f);
        return;
    }
    uint8_t* buf = (uint8_t*)malloc(RDRAM_SNAP_SIZE);
    if (buf == NULL) {
        fprintf(stderr, "[savestate] load: out of memory\n");
        fclose(f);
        return;
    }
    size_t got = fread(buf, 1, RDRAM_SNAP_SIZE, f);
    fclose(f);
    if (got != RDRAM_SNAP_SIZE) {
        fprintf(stderr, "[savestate] load: %s truncated (%zu/%u bytes)\n",
                path, got, RDRAM_SNAP_SIZE);
        free(buf);
        return;
    }
    memcpy(rdram, buf, RDRAM_SNAP_SIZE);
    free(buf);
    // Repair the native OSThread.context pointers the memcpy just clobbered with the save
    // process's addresses; without this the scheduler dereferences garbage on the next tick.
    ultramodern_relink_thread_contexts(rdram);
    fprintf(stderr, "[savestate] loaded %u bytes from %s (scene=%u)\n",
            RDRAM_SNAP_SIZE, path, h.scene);
}

// SDL-thread entry points (edge-detected in main.cpp). Only flip the request bit; the copy
// runs on the game thread at the next frame boundary.
void aero_savestate_request_save(void) {
    atomic_fetch_or_explicit(&g_req, REQ_SAVE, memory_order_relaxed);
}
void aero_savestate_request_load(void) {
    atomic_fetch_or_explicit(&g_req, REQ_LOAD, memory_order_relaxed);
}

// Per-frame tick, injected at func_80015C8C entry +1 instruction (0x80015C90), so it
// coexists with the warp hook at the same function -- N64Recomp rejects two hooks on the
// exact same vram. Runs on the game thread with rdram+ctx live, before this frame's scene
// request is consumed.
void aero_savestate_tick(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;

    // Env-driven headless triggers, parsed once. AERO_STATE_LOAD=<file> auto-loads at boot;
    // AERO_STATE_SAVE=<file> auto-saves a spot without a keypress (an agent can pair either
    // with AERO_WARP to capture/return to a race deterministically).
    static int env_checked;
    static const char* env_load;
    static int load_min_scene;      // auto-load once scene >= this (AERO_STATE_LOAD_SCENE)
    static int load_delay;          // ticks to wait once eligible (AERO_STATE_LOAD_DELAY)
    static int load_ticks;          // ticks counted while eligible so far
    static const char* env_save;
    static int save_target_scene;   // scene to snapshot at (AERO_STATE_SAVE_SCENE, default 5)
    static int save_delay;          // ticks to wait once in that scene (AERO_STATE_SAVE_DELAY)
    static int save_ticks;          // ticks counted at the target scene so far
    if (!env_checked) {
        env_checked = 1;
        const char* l = getenv("AERO_STATE_LOAD");
        env_load = (l != NULL && l[0] != '\0') ? l : NULL;
        const char* lm = getenv("AERO_STATE_LOAD_SCENE");
        load_min_scene = (lm != NULL) ? atoi(lm) : 2;   // 2 = past the boot logos by default
        const char* ld = getenv("AERO_STATE_LOAD_DELAY");
        load_delay = (ld != NULL) ? atoi(ld) : 0;
        const char* s = getenv("AERO_STATE_SAVE");
        env_save = (s != NULL && s[0] != '\0') ? s : NULL;
        const char* st = getenv("AERO_STATE_SAVE_SCENE");
        save_target_scene = (st != NULL) ? atoi(st) : 5;   // 5 = in-race (see aero_warp.c)
        const char* d = getenv("AERO_STATE_SAVE_DELAY");
        save_delay = (d != NULL) ? atoi(d) : 0;
    }

    int scene = (int32_t)MEM_W(0, (gpr)(int32_t)SCENE_CUR);

    // Settled = the scene manager is quiescent: no transition in flight, the same scene for
    // 30+ ticks (the warp's empirical fade-length SCAFFOLD, same rationale), and -- for the
    // race scene -- the phase walk 1->2 (loading) has reached 3 (running; dispatch at
    // 0x800161C4, see aero_warp.c). Measured consequence of skipping this: a load fired at
    // the 2-VI transient boot scene 9, or a save taken at race phase 2, crashes the restore
    // (aspMain DMEM wrap / native fault) because the loader threads' native state does not
    // match the restored RAM. Every trigger below fires only from a settled frame.
    uint32_t req_scene = (uint32_t)MEM_W(0, (gpr)(int32_t)SCENE_REQ);
    uint32_t phase     = (uint32_t)MEM_W(0, (gpr)(int32_t)SCENE_PHASE);
    static uint32_t stable_scene = ~0u;
    static int stable_ticks;
    if ((uint32_t)scene != stable_scene || (uint32_t)scene != req_scene) {
        stable_scene = ((uint32_t)scene == req_scene) ? (uint32_t)scene : ~0u;
        stable_ticks = 0;
    }
    stable_ticks++;
    int settled = ((uint32_t)scene == req_scene) && stable_ticks >= 30 &&
                  (scene != 5 || phase == 3);

    if (env_load != NULL && scene >= load_min_scene && settled) {
        if (load_ticks++ >= load_delay) {
            const char* p = env_load;
            env_load = NULL;      // fire once
            do_load(rdram, p);
            return;               // don't also process a same-frame save/hotkey request
        }
    }

    if (env_save != NULL && scene == save_target_scene && settled) {
        if (save_ticks++ >= save_delay) {
            const char* p = env_save;
            env_save = NULL;      // fire once
            do_save(rdram, p);
            return;
        }
    }

    // Manual F7/F8. Hold the request PENDING (don't consume it) until we are past the boot
    // logos (scene >= 2, so the game's threads exist and a load's relink can repair them)
    // AND the scene is settled (see above). A keypress during boot or a scene transition
    // simply fires on the first settled frame.
    if (scene < 2 || !settled) return;
    uint32_t req = atomic_exchange_explicit(&g_req, 0, memory_order_relaxed);
    if (req == 0) return;
    if (req & REQ_LOAD) do_load(rdram, slot_path());   // load wins if both somehow set
    else if (req & REQ_SAVE) do_save(rdram, slot_path());
}
