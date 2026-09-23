#include "aero_shadow_depth.h"

#include <cstring>

namespace {

constexpr uint32_t kRdramAddressMask = 0x03FFFFFFu;
constexpr uint32_t kMaxDisplayListCommands = 200000u;

// F3DEX2 command words and the two known depth-writing course render modes.
constexpr uint32_t kSetRenderModeCommand = 0xB900031Du;
constexpr uint32_t kCourseOpaqueRenderMode = 0xC8112078u;
constexpr uint32_t kCourseEdgeRenderMode = 0xC8110038u;
constexpr uint32_t kCarRenderMode = 0x00552078u;
constexpr uint32_t kShadowRenderMode = 0x00504240u;
constexpr uint32_t kSetGeometryModeCommand = 0xB7000000u;
constexpr uint32_t kSetOtherModeHighRenderModeCommand = 0xBA001402u;
constexpr uint32_t kShadowGeometryMode = 0x00002000u;
constexpr uint32_t kZBuffer = 0x00000001u;
constexpr uint32_t kZCompare = 0x00000010u;
constexpr uint32_t kZModeDecal = 0x00000C00u;
constexpr uint32_t kShadowDepthCompare = kZModeDecal | kZCompare;
constexpr uint32_t kEndDisplayListOpcode = 0xB8u;

uint32_t read_word(const uint8_t* rdram, uint32_t offset) {
    uint32_t value;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

void write_word(uint8_t* rdram, uint32_t offset, uint32_t value) {
    std::memcpy(rdram + offset, &value, sizeof(value));
}

} // namespace

void aero_patch_racer_shadow_depth(uint8_t* rdram, uint32_t rdram_size,
                                   const OSTask* task) {
    if (rdram == nullptr || task == nullptr) return;

    const uint32_t task_start = static_cast<uint32_t>(task->t.data_ptr) & kRdramAddressMask;
    if ((task_start & 7u) != 0 || task_start > rdram_size) return;

    const uint32_t available_commands = (rdram_size - task_start) / 8u;
    const uint32_t command_count = available_commands < kMaxDisplayListCommands
        ? available_commands
        : kMaxDisplayListCommands;
    bool saw_depth_course = false;

    for (uint32_t cmd = 0; cmd < command_count; ++cmd) {
        const uint32_t offset = task_start + cmd * 8u;
        const uint32_t w0 = read_word(rdram, offset);
        const uint32_t w1 = read_word(rdram, offset + 4u);
        if ((w0 >> 24) == kEndDisplayListOpcode) return;
        if (w0 != kSetRenderModeCommand) continue;

        if (w1 == kCourseOpaqueRenderMode || w1 == kCourseEdgeRenderMode) {
            saw_depth_course = true;
            continue;
        }
        if (w1 == kCarRenderMode) {
            // A combined split-screen root may contain another course pass
            // after P1's cars. Require a fresh course marker for each view so
            // the later 2D HUD's shared shadow mode remains untouched.
            saw_depth_course = false;
            continue;
        }
        // The exact current pattern needs a course marker before its two
        // prefix commands, but keep the prefix task-relative even if that
        // surrounding sequence changes later.
        if (!saw_depth_course || w1 != kShadowRenderMode || cmd < 2u) continue;

        // Match the race shadow setup exactly, with the prefix constrained to
        // this task's display list (cmd >= 2), before changing guest RDRAM.
        const uint32_t geometry_offset = offset - 16u;
        const uint32_t other_mode_offset = offset - 8u;
        if (read_word(rdram, geometry_offset) != kSetGeometryModeCommand ||
            read_word(rdram, geometry_offset + 4u) != kShadowGeometryMode ||
            read_word(rdram, other_mode_offset) != kSetOtherModeHighRenderModeCommand ||
            read_word(rdram, other_mode_offset + 4u) != 0u) {
            continue;
        }

        write_word(rdram, geometry_offset + 4u,
                   kShadowGeometryMode | kZBuffer);
        // Projected racer shadows are coplanar with the course. RT64 maps an
        // ordinary Z_CMP to strict LESS, which can flicker on equal-depth road
        // pixels. ZMODE_DEC uses RT64's coplanar-depth tolerance instead. A
        // nearer wall still fails that depth match; no depth writes are needed.
        write_word(rdram, offset + 4u, w1 | kShadowDepthCompare);
        // Keep scanning: a combined split-screen task can contain another
        // course/shadow pass after this one.
    }
}
