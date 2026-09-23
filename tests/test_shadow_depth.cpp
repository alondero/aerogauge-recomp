#include "aero_shadow_depth.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::abort(); \
        } \
    } while (false)

namespace {

constexpr uint32_t kTaskStart = 32;
constexpr uint32_t kSetRenderMode = 0xB900031Du;
constexpr uint32_t kCourseOpaque = 0xC8112078u;
constexpr uint32_t kCourseEdge = 0xC8110038u;
constexpr uint32_t kCarMode = 0x00552078u;
constexpr uint32_t kShadowMode = 0x00504240u;
constexpr uint32_t kEndDisplayList = 0xB8000000u;

using Rdram = std::array<uint32_t, 64>;

void put_command(Rdram& rdram, uint32_t byte_offset, uint32_t w0, uint32_t w1) {
    std::memcpy(reinterpret_cast<uint8_t*>(rdram.data()) + byte_offset, &w0, sizeof(w0));
    std::memcpy(reinterpret_cast<uint8_t*>(rdram.data()) + byte_offset + 4, &w1, sizeof(w1));
}

uint32_t read_word(const Rdram& rdram, uint32_t byte_offset) {
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const uint8_t*>(rdram.data()) + byte_offset,
                sizeof(value));
    return value;
}

void setup_shadow_pass(Rdram& rdram, uint32_t byte_offset, uint32_t mode = kShadowMode) {
    put_command(rdram, byte_offset, kSetRenderMode, kCourseOpaque);
    put_command(rdram, byte_offset + 8, 0xB7000000u, 0x00002000u);
    put_command(rdram, byte_offset + 16, 0xBA001402u, 0);
    put_command(rdram, byte_offset + 24, kSetRenderMode, mode);
}

OSTask task_at(uint32_t byte_offset) {
    OSTask task{};
    task.t.data_ptr = static_cast<int32_t>(byte_offset);
    return task;
}

void test_exact_match_enables_depth_and_decal_compare() {
    Rdram rdram{};
    setup_shadow_pass(rdram, kTaskStart);
    put_command(rdram, kTaskStart + 32, kEndDisplayList, 0);
    OSTask task = task_at(kTaskStart);

    aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(rdram.data()), sizeof(rdram), &task);

    CHECK(read_word(rdram, kTaskStart + 12) == 0x00002001u); // preserve flags, add G_ZBUFFER
    CHECK(read_word(rdram, kTaskStart + 28) == (kShadowMode | 0xC10u)); // ZMODE_DEC | Z_CMP
}

void test_fail_closed_on_changed_sequences_and_missing_course_mode() {
    for (int variant = 0; variant < 5; ++variant) {
        Rdram rdram{};
        setup_shadow_pass(rdram, kTaskStart);
        if (variant == 0) {
            put_command(rdram, kTaskStart + 8, 0xB7000000u, 0x00002002u);
        } else if (variant == 1) {
            put_command(rdram, kTaskStart + 16, 0xBA001402u, 1);
        } else if (variant == 2) {
            put_command(rdram, kTaskStart, kSetRenderMode, 0xDEADBEEFu);
        } else if (variant == 3) {
            put_command(rdram, kTaskStart + 24, kSetRenderMode, kShadowMode ^ 1u);
        } else {
            // Alter command order so the exact geometry/other-mode prefix is absent.
            put_command(rdram, kTaskStart + 8, 0xBA001402u, 0);
            put_command(rdram, kTaskStart + 16, 0xB7000000u, 0x00002000u);
        }
        put_command(rdram, kTaskStart + 32, kEndDisplayList, 0);
        OSTask task = task_at(kTaskStart);
        const Rdram before = rdram;

        aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(rdram.data()), sizeof(rdram), &task);

        CHECK(rdram == before);
    }
}

void test_multiple_viewports_and_same_mode_hud_pass() {
    Rdram rdram{};
    setup_shadow_pass(rdram, kTaskStart);
    put_command(rdram, kTaskStart + 32, kSetRenderMode, kCarMode);

    // HUD reuses the shadow render mode and prefix, but has no fresh course mode.
    put_command(rdram, kTaskStart + 40, 0xB7000000u, 0x00002000u);
    put_command(rdram, kTaskStart + 48, 0xBA001402u, 0);
    put_command(rdram, kTaskStart + 56, kSetRenderMode, kShadowMode);

    // A second viewport starts another course/shadow/car sequence in this root list.
    put_command(rdram, kTaskStart + 64, kSetRenderMode, kCourseEdge);
    put_command(rdram, kTaskStart + 72, 0xB7000000u, 0x00002000u);
    put_command(rdram, kTaskStart + 80, 0xBA001402u, 0);
    put_command(rdram, kTaskStart + 88, kSetRenderMode, kShadowMode);
    put_command(rdram, kTaskStart + 96, kSetRenderMode, kCarMode);
    put_command(rdram, kTaskStart + 104, kEndDisplayList, 0);
    OSTask task = task_at(kTaskStart);

    aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(rdram.data()), sizeof(rdram), &task);

    CHECK(read_word(rdram, kTaskStart + 12) == 0x00002001u);
    CHECK(read_word(rdram, kTaskStart + 28) == (kShadowMode | 0xC10u));
    CHECK(read_word(rdram, kTaskStart + 44) == 0x00002000u);
    CHECK(read_word(rdram, kTaskStart + 60) == kShadowMode);
    CHECK(read_word(rdram, kTaskStart + 76) == 0x00002001u);
    CHECK(read_word(rdram, kTaskStart + 92) == (kShadowMode | 0xC10u));
}

void test_null_unaligned_short_and_end_boundary_inputs() {
    Rdram rdram{};
    setup_shadow_pass(rdram, kTaskStart);
    put_command(rdram, kTaskStart + 32, kEndDisplayList, 0);
    const Rdram before = rdram;

    aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(rdram.data()), sizeof(rdram), nullptr);
    CHECK(rdram == before);
    OSTask unaligned = task_at(kTaskStart + 1);
    aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(rdram.data()), sizeof(rdram), &unaligned);
    CHECK(rdram == before);

    OSTask short_buffer_task = task_at(0);
    aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(rdram.data()), 15,
                                  &short_buffer_task);
    CHECK(rdram == before);

    Rdram ended{};
    put_command(ended, kTaskStart, kEndDisplayList, 0);
    setup_shadow_pass(ended, kTaskStart + 8);
    OSTask end_task = task_at(kTaskStart);
    const Rdram ended_before = ended;
    aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(ended.data()), sizeof(ended), &end_task);
    CHECK(ended == ended_before);
}

void test_prefix_memory_before_task_is_never_mutated() {
    Rdram rdram{};
    put_command(rdram, 0, kSetRenderMode, kCourseOpaque);
    put_command(rdram, 16, 0xB7000000u, 0x00002000u);
    put_command(rdram, 24, 0xBA001402u, 0);
    put_command(rdram, kTaskStart, kSetRenderMode, kShadowMode);
    put_command(rdram, kTaskStart + 8, kEndDisplayList, 0);
    const Rdram before = rdram;
    OSTask task = task_at(kTaskStart);

    aero_patch_racer_shadow_depth(reinterpret_cast<uint8_t*>(rdram.data()), sizeof(rdram), &task);

    CHECK(rdram == before);
}

} // namespace

int main() {
    test_exact_match_enables_depth_and_decal_compare();
    test_fail_closed_on_changed_sequences_and_missing_course_mode();
    test_multiple_viewports_and_same_mode_hud_pass();
    test_null_unaligned_short_and_end_boundary_inputs();
    test_prefix_memory_before_task_is_never_mutated();
    return 0;
}
