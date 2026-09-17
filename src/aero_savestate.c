// Developer save-state support.
//
// Ownership: the SDL/main thread publishes an atomic F7/F8 request. The game
// thread consumes it at the per-frame scene-driver hook and copies the low
// 8 MiB guest RDRAM. N64Recomp memory layout and byte order are preserved by
// the raw byte copy; no host pointer is serialized.
//
// This is not a resumable native save. Guest OSThread register files, native
// stacks, RT64 state, audio state, and in-flight background work remain in the
// current process. Loading therefore requires a settled scene and can still
// race native readers. A missing or invalid file is rejected before RDRAM is
// changed. The feature is for repeatable debugging, not player quicksaves.
//
// F7/F8 use the local slot path. AERO_STATE_SAVE and AERO_STATE_LOAD provide
// one-shot headless operations; AERO_STATE_FILE overrides the interactive
// slot. The full safety contract is in docs/debugging.md and
// docs/reference/runtime.md.

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

// The runtime's native osGetTime is a 64-bit monotonic clock with a
// process-local epoch. RAM-resident samples therefore need rebasing on load;
// the current anchor is listed in docs/reference/rom.md.
extern uint64_t osGetTime(void);

#define SCENE_CUR   0x8013FF80u       // u32 current scene (see src/aero_warp.c; race = 5)
#define SCENE_REQ   0x8013FF84u       // u32 requested scene (cur != req => transition in flight)
#define SCENE_PHASE 0x8013FF88u       // u32 scene-local phase; race walks 1->2 (loading) -> 3 (running)
#define RDRAM_SNAP_SIZE 0x800000u     // low 8 MiB = guest-addressable N64 RAM

// osGetTime anchors stored in guest RAM (64-bit, high word first). The race
// timekeeper uses the per-frame delta to advance the race clock, so do_load
// rebases each known anchor by the save/load process-epoch difference. Add any
// newly discovered RAM-resident anchor to this list and update the ROM
// reference and its test evidence.
static const uint32_t k_ostime_anchors[] = {
    0x8016C4F0u,                      // race timekeeper's last-frame sample (func_8001D7F0, pair 0x8016C4F0/F4)
};

// On-disk header (32 bytes). It is host-endian and intentionally local to a
// debug build. Magic, version, and size are validated before any RDRAM is
// touched, so a truncated or unrelated file cannot corrupt the guest image.
#define STATE_MAGIC "AEROSTAT"
#define STATE_VERSION 2u
typedef struct {
    char     magic[8];      // "AEROSTAT"
    uint32_t version;       // STATE_VERSION
    uint32_t rdram_size;    // RDRAM_SNAP_SIZE
    uint32_t scene;         // captured scene word (informational)
    uint32_t reserved;
    uint64_t os_time;       // save-process osGetTime() at snapshot; anchors rebase on load
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
    h.os_time    = osGetTime();
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
    // Rebase the RAM-resident osGetTime anchors from the save process's clock epoch to
    // ours (see k_ostime_anchors); without this the race clock jumps to time-over.
    {
        uint64_t delta = osGetTime() - h.os_time;
        for (size_t i = 0; i < sizeof(k_ostime_anchors) / sizeof(k_ostime_anchors[0]); i++) {
            uint32_t hi_addr = k_ostime_anchors[i];
            uint64_t t = ((uint64_t)(uint32_t)MEM_W(0, (gpr)(int32_t)hi_addr) << 32) |
                          (uint64_t)(uint32_t)MEM_W(4, (gpr)(int32_t)hi_addr);
            t += delta;
            MEM_W(0, (gpr)(int32_t)hi_addr) = (int32_t)(uint32_t)(t >> 32);
            MEM_W(4, (gpr)(int32_t)hi_addr) = (int32_t)(uint32_t)t;
        }
    }
    fprintf(stderr, "[savestate] loaded %u bytes from %s (scene=%u)\n",
            RDRAM_SNAP_SIZE, path, h.scene);
}

// SDL-thread entry points (edge-detected in main.cpp). Only flip the request bit; the copy
// runs on the game thread at the next frame boundary. Acknowledge the keypress IMMEDIATELY
// on stderr: the settled gate in the tick can legitimately hold a request for many seconds
// (results screen / countdown / scene transition are never "settled"), and without this
// line a held request is indistinguishable from a dead hotkey.
void aero_savestate_request_save(void) {
    atomic_fetch_or_explicit(&g_req, REQ_SAVE, memory_order_relaxed);
    fprintf(stderr, "[savestate] F7: save requested (slot %s); fires at the next settled frame\n",
            slot_path());
}
void aero_savestate_request_load(void) {
    atomic_fetch_or_explicit(&g_req, REQ_LOAD, memory_order_relaxed);
    fprintf(stderr, "[savestate] F8: load requested (slot %s); fires at the next settled frame\n",
            slot_path());
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
    static int load_at_scene;       // auto-load once settled AT this scene (AERO_STATE_LOAD_SCENE)
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
        // Match one scene exactly. Scene ids are not ordered by progress, so a
        // floor could fire in the attract demo. Default 3 is the title screen,
        // the first calm settled scene.
        const char* lm = getenv("AERO_STATE_LOAD_SCENE");
        load_at_scene = (lm != NULL) ? atoi(lm) : 3;
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

    // Settled = no scene transition, the same scene for at least 30 ticks, and
    // race phase 3 when the scene is a race. This conservative port condition
    // avoids copying guest memory while the loader or race setup is active.
    // It is not a decoded ROM ready signal; replacing it requires a focused
    // regression check for transitions and native readers.
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

    if (env_load != NULL && scene == load_at_scene && settled) {
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
    // simply fires on the first settled frame. While the gate is closed, say so ONCE per
    // pending request -- the wait can last many seconds (or forever on a results screen),
    // and silence here reads as a dead hotkey.
    if (scene < 2 || !settled) {
        static int held_logged;
        uint32_t pending = atomic_load_explicit(&g_req, memory_order_relaxed);
        if (pending != 0 && !held_logged) {
            held_logged = 1;
            fprintf(stderr, "[savestate] request held: scene not settled "
                            "(scene=%d req=%u phase=%u stable_ticks=%d); fires when it settles\n",
                    scene, req_scene, phase, stable_ticks);
        } else if (pending == 0) {
            held_logged = 0;
        }
        return;
    }
    uint32_t req = atomic_exchange_explicit(&g_req, 0, memory_order_relaxed);
    if (req == 0) return;
    if (req & REQ_LOAD) do_load(rdram, slot_path());   // load wins if both somehow set
    else if (req & REQ_SAVE) do_save(rdram, slot_path());
}
