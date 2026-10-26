// Behavioural spec for the scheduler audio-backlog recovery in
// src/aero_audio_sched.c, hooked at the entry of the ROM's stock libultra
// __scSchedule (USA func_80065240, Japan Rev A 0x80065BE8).
//
// The model below reproduces the two ROM rules that matter, read from the
// disassembly: __scAppendList (USA func_800650D8) sets doAudio whenever it
// appends an audio task, and __scSchedule clears doAudio when it takes the
// audio list head. One append therefore buys exactly one audio start. The
// retrace handler appends the task the audio thread posted last frame; the
// RSP-done handler calls __scSchedule again. A port-side boot stall that leaves
// two tasks queued never recovers without the hook.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

#undef NDEBUG
#include <assert.h>

extern "C" void aero_audio_sched_schedule(uint8_t* rdram, recomp_context* ctx);
extern "C" void aero_audio_sched_reset_for_test(int enabled);

#define RDRAM_SIZE (8u * 1024u * 1024u)
#define SC 0x801A8868u // the ROM's OSSched (osCreateScheduler a0)
#define SC_AUDIO_HEAD 0x264u
#define SC_AUDIO_TAIL 0x26Cu
#define SC_DO_AUDIO 0x280u
#define TASK_BASE 0x800F6A88u
#define TASK_NEXT 0x0u

static uint8_t* rdram;

static uint32_t off(uint32_t a) { return a - 0x80000000u; }
static void w32(uint32_t a, uint32_t v) { *(uint32_t*)(rdram + off(a)) = v; }
static uint32_t r32(uint32_t a) { return *(uint32_t*)(rdram + off(a)); }

static void hook(uint32_t sc) {
    recomp_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.r4 = (gpr)(int32_t)sc;
    aero_audio_sched_schedule(rdram, &ctx);
}

// __scAppendList for an audio task.
static void append_audio(uint32_t task) {
    w32(task + TASK_NEXT, 0);
    const uint32_t tail = r32(SC + SC_AUDIO_TAIL);
    if (tail != 0) w32(tail + TASK_NEXT, task); else w32(SC + SC_AUDIO_HEAD, task);
    w32(SC + SC_AUDIO_TAIL, task);
    w32(SC + SC_DO_AUDIO, 1);
}

// __scSchedule's audio branch with the RSP free and no parallel gfx task,
// preceded by the entry hook. Returns the started task or 0.
static uint32_t schedule_audio(bool with_hook) {
    if (with_hook) hook(SC);
    if (r32(SC + SC_DO_AUDIO) == 0) return 0;
    const uint32_t task = r32(SC + SC_AUDIO_HEAD);
    w32(SC + SC_DO_AUDIO, 0);
    const uint32_t next = r32(task + TASK_NEXT);
    w32(SC + SC_AUDIO_HEAD, next);
    if (next == 0) w32(SC + SC_AUDIO_TAIL, 0);
    return task;
}

static int queued(void) {
    int n = 0;
    for (uint32_t t = r32(SC + SC_AUDIO_HEAD); t != 0; t = r32(t + TASK_NEXT)) n++;
    return n;
}

static void reset(int enabled) {
    memset(rdram, 0, RDRAM_SIZE);
    aero_audio_sched_reset_for_test(enabled);
}

// One retrace: append last frame's task, start what the scheduler allows, and
// keep scheduling on each synchronous RSP-done exactly as the port runs audio.
static int retrace(uint32_t posted_task, bool with_hook) {
    append_audio(posted_task);
    int started = 0;
    while (schedule_audio(with_hook) != 0) started++;
    return started;
}

static void backlog_persists_without_recovery(void) {
    reset(0);
    // Boot stall: two posted tasks are already queued when steady state starts.
    append_audio(TASK_BASE + 0x000);
    append_audio(TASK_BASE + 0x7C0);
    w32(SC + SC_DO_AUDIO, 0);
    for (int frame = 0; frame < 120; frame++) {
        retrace(TASK_BASE + 0x7C0u * (uint32_t)((frame + 2) % 3), false);
        assert(queued() == 2 && "stock rules leave the backlog queued forever");
    }
}

static void backlog_drains_on_first_schedule(void) {
    reset(1);
    append_audio(TASK_BASE + 0x000);
    append_audio(TASK_BASE + 0x7C0);
    w32(SC + SC_DO_AUDIO, 0);
    assert(retrace(TASK_BASE + 0xF80, true) == 3);
    assert(queued() == 0);
    for (int frame = 0; frame < 120; frame++) {
        assert(retrace(TASK_BASE + 0x7C0u * (uint32_t)(frame % 3), true) == 1);
        assert(queued() == 0 && "after recovery each task starts one retrace after posting");
    }
}

static void normal_state_is_unchanged(void) {
    reset(1);
    // Nothing queued: doAudio stays clear.
    hook(SC);
    assert(r32(SC + SC_DO_AUDIO) == 0);
    // Freshly appended task: the ROM already set doAudio, the hook leaves it.
    append_audio(TASK_BASE);
    hook(SC);
    assert(r32(SC + SC_DO_AUDIO) == 1);
    assert(queued() == 1);
}

static void kill_switch_keeps_rom_behaviour(void) {
    reset(0);
    append_audio(TASK_BASE);
    w32(SC + SC_DO_AUDIO, 0);
    hook(SC);
    assert(r32(SC + SC_DO_AUDIO) == 0);
}

static void rejects_invalid_scheduler_pointers(void) {
    reset(1);
    const uint32_t bad[] = {0u, 0x00001000u, 0x807FFF00u, SC + 2u};
    for (uint32_t sc : bad) hook(sc);
    for (uint32_t i = 0; i < RDRAM_SIZE; i += 4) assert(*(uint32_t*)(rdram + i) == 0);
}

int main(void) {
    rdram = static_cast<uint8_t*>(calloc(1, RDRAM_SIZE));
    assert(rdram != nullptr);
    backlog_persists_without_recovery();
    backlog_drains_on_first_schedule();
    normal_state_is_unchanged();
    kill_switch_keeps_rom_behaviour();
    rejects_invalid_scheduler_pointers();
    free(rdram);
    puts("test_audio_sched: OK");
    return 0;
}
