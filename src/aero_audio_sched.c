// Scheduler audio-backlog recovery, hooked at the entry of the ROM's stock
// libultra __scSchedule (USA func_80065240, Japan Rev A 0x80065BE8). a0 is the
// game's single OSSched (0x801A8868 in USA, created by func_80065884).
//
// ROM rules (both regions; see docs/reference/audio.md):
//   - __scAppendList sets doAudio (+0x280) whenever it appends an audio task;
//   - __scSchedule starts the audio list head (+0x264) only while doAudio is
//     set, and clears doAudio when it does.
// One append therefore buys exactly one audio start. On console the audio
// thread posts one task per retrace and the scheduler starts it at the next
// retrace, so the list never holds two tasks. The port runs game threads
// cooperatively; a boot stall can deliver several retraces in one burst, the
// audio thread then posts two or three tasks before the scheduler starts one,
// and the stock rules keep that backlog for the rest of the session. A task
// that waits three retraces finds its command list rebuilt by the audio
// thread (two command lists, three frame records), runs another frame's
// commands, and writes stale or misplaced PCM: the reported crackling.
//
// Deliberate divergence: while an audio task is queued and doAudio is clear,
// set doAudio so the queued task starts on this schedule. This state is
// unreachable on console, so a console-timed session is unchanged. In the port
// the backlog drains on the next RSP-done and audio returns to one retrace of
// latency. AERO_AUDIO_BACKLOG_DRAIN=0 keeps the ROM rule for comparison.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "recomp.h"

#define SC_AUDIO_LIST_HEAD 0x264u
#define SC_DO_AUDIO        0x280u
#define SC_SIZE            0x284u

// -1 = not read yet. Hooks run on the scheduler's game thread only.
static int g_enabled = -1;
static int g_drain_logged = 0;

void aero_audio_sched_reset_for_test(int enabled) {
    g_enabled = enabled;
    g_drain_logged = 0;
}

static int drain_enabled(void) {
    if (g_enabled < 0) {
        const char* value = getenv("AERO_AUDIO_BACKLOG_DRAIN");
        g_enabled = !(value != NULL && value[0] == '0' && value[1] == '\0');
    }
    return g_enabled;
}

void aero_audio_sched_schedule(uint8_t* rdram, recomp_context* ctx) {
    if (rdram == NULL || ctx == NULL || !drain_enabled()) return;
    // Guest pointers are 32-bit KSEG0 addresses carried in a 64-bit gpr.
    const gpr sc = (gpr)(int32_t)ctx->r4;
    const uint32_t address = (uint32_t)sc;
    if (address < 0x80000400u || address > 0x80800000u - SC_SIZE || (address & 3u) != 0) return;
    if (MEM_W(SC_AUDIO_LIST_HEAD, sc) == 0 || MEM_W(SC_DO_AUDIO, sc) != 0) return;
    MEM_W(SC_DO_AUDIO, sc) = 1;
    if (!g_drain_logged) {
        g_drain_logged = 1;
        fprintf(stderr, "[probe] audio: scheduler audio backlog detected; starting queued task\n");
    }
}
