// Exercise production task submission without a renderer or ROM. The game reuses
// its OSTask and audio command buffer as soon as submission returns.
#undef NDEBUG
#include <array>
#include <cassert>
#include <cstdio>
#include <thread>
#include "ultramodern/ultramodern.hpp"

namespace {
std::thread::id submitting_thread;
unsigned executions = 0, completions = 0;
bool task_succeeds = true;
uint32_t expected_size = 0;
constexpr int32_t TaskAddress = static_cast<int32_t>(0x80000100u);
constexpr int32_t QueueAddress = static_cast<int32_t>(0x80000200u);
constexpr OSMesg CompletionMessage = 0x1234;
}

namespace ultramodern::rsp {
bool run_task(uint8_t*, const OSTask* task) {
    assert(std::this_thread::get_id() == submitting_thread);
    assert(task->t.type == M_AUDTASK);
    assert(task->t.data_size == expected_size);
    assert(completions == executions);
    ++executions;
    return task_succeeds;
}
}

namespace ultramodern {
void enqueue_external_message_src(PTR(OSMesgQueue) queue, OSMesg message,
                                  bool jam, EventMessageSource source) {
    assert(queue == QueueAddress && message == CompletionMessage);
    assert(!jam && source == EventMessageSource::Sp);
    assert(executions == completions + 1);
    ++completions;
}
}

int main() {
    alignas(OSTask) std::array<uint8_t, 1024> memory{};
    auto* rdram = memory.data();
    auto* task = TO_PTR(OSTask, TaskAddress);
    submitting_thread = std::this_thread::get_id();
    osSetEventMesg(rdram, OS_EVENT_SP, QueueAddress, CompletionMessage);

    // Successful, failed, then successful tasks must all finish and signal SP
    // before the caller reuses the descriptor. Failure must not exit the game.
    for (unsigned i = 0; i < 3; ++i) {
        task_succeeds = i != 1;
        expected_size = 0x120 + i * 8;
        task->t.type = M_AUDTASK;
        task->t.data_size = expected_size;
        ultramodern::submit_rsp_task(rdram, TaskAddress);
        assert(executions == i + 1 && completions == i + 1);
        task->t.data_size = 0; // Reuse immediately, like the game.
    }
    std::puts("PASS: audio tasks execute before reuse and complete after failure");
}
