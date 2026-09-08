// Exercise the real display-list retag pass with ROM-derived message geometry.
#undef NDEBUG
#include <assert.h>
#include "../src/aero_hud_widescreen.c"

uint32_t aero_ws_get_hud_rect_aspect_bits(void) { return 0; }

static uint8_t ram[8 * 1024 * 1024];

static void rect(gpr* cur, int x, int y, int width) {
    emit_at(ram, cur, 0xE4000000u | ((x + width) * 4u << 12) | ((y + 10) * 4u),
            (x * 4u << 12) | (y * 4u));
    emit_at(ram, cur, 0xB4000000u, 0);
    emit_at(ram, cur, 0xB3000000u, 0x04000400u);
}

// Decode actual emitted align commands, then check every rect's effective origin.
static void check_origins(gpr start, gpr end, int message_rects) {
    uint8_t* rdram = ram;
    unsigned origin = G_EX_ORIGIN_NONE;
    int seen = 0;
    for (gpr p = start; p < end; p += 8) {
        uint32_t w0 = MEM_W(0, p), w1 = MEM_W(4, p);
        if (w0 == ((RT64_EXTENDED_OPCODE << 24) | G_EX_SETRECTALIGN_V1)) {
            origin = w1 & 0xFFFu;
            p += 8; // align payload is not a command
        } else if ((w0 >> 24) == 0xE4) {
            unsigned expected = seen == 0 ? G_EX_ORIGIN_RIGHT :
                seen <= message_rects ? G_EX_ORIGIN_NONE : G_EX_ORIGIN_LEFT;
            assert(origin == expected);
            // Re-emission must preserve the texrect's texture-coordinate payload.
            assert((uint32_t)MEM_W(8, p) == 0xB4000000u);
            assert((uint32_t)MEM_W(16, p) == 0xB3000000u);
            assert((uint32_t)MEM_W(20, p) == 0x04000400u);
            seen++;
        }
    }
    assert(seen == message_rects + 2);
    assert(origin == G_EX_ORIGIN_NONE);
}

int main(void) {
    uint8_t* rdram = ram;
    const gpr start = (gpr)(int32_t)0x80200000u;
    gpr cur = start;
    MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = (int32_t)start;
    aero_ws_hud_scan_begin(ram, NULL);
    aero_ws_message_begin(ram, cur);
    // func_8001024C/80010C88: notification origin (116,131).
    // Font 0x8008C31C has 14x10 glyphs; func_8001F790 advances 14 per letter.
    for (int i = 0; i < 9; i++) rect(&cur, 116 + 14 * i, 131, 14);
    aero_ws_message_end(ram, cur);
    MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = cur;
    aero_ws_retag_rects(ram, start, cur);
    gpr end = MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER);
    assert(end == cur); // no pin brackets for a centred announcement

    // Message ranges must survive insertion of an earlier HUD bracket. Include
    // multiple ranges, multi-call text, and tiles on both sides of the deadband.
    cur = start;
    MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = cur;
    aero_ws_hud_scan_begin(ram, NULL);
    rect(&cur, 247, 172, 53); // dial: remains RIGHT
    int messages = 0;
    const int origins[] = {116, 106, 124, 132, 84};
    const int lengths[] = {9, 9, 9, 7, 13};
    // Final lap, wrong way, ending banners, and lap ordinal/time layouts.
    for (unsigned m = 0; m < sizeof(origins) / sizeof(origins[0]); m++) {
        aero_ws_message_begin(ram, cur);
        for (int i = 0; i < lengths[m]; i++) {
            rect(&cur, origins[m] + 14 * i, 112, 14);
            messages++;
        }
        aero_ws_message_end(ram, cur);
    }
    aero_ws_message_begin(ram, cur); // N + LAPS + LEFT are one ownership range
    rect(&cur, 116, 131, 14);
    rect(&cur, 134, 134, 32);
    rect(&cur, 170, 134, 32);
    messages += 3;
    aero_ws_message_end(ram, cur);
    aero_ws_message_begin(ram, cur); // tiled central sprite spanning both thresholds
    rect(&cur, 89, 103, 10);
    rect(&cur, 200, 103, 20);
    messages += 2;
    aero_ws_message_end(ram, cur);
    rect(&cur, 20, 194, 44); // GLPS: remains LEFT
    MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = cur;
    MEM_W(0, (gpr)(int32_t)AERO_SCENE_CUR) = 5;
    MEM_W(0, (gpr)(int32_t)AERO_SCENE_PHASE) = 3;
    aero_ws_hud_frame_end(ram, NULL);
    end = MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER);
    assert(end == cur + 2 * 80); // only the two edge HUD groups add brackets
    check_origins(start, end, messages);

    // Reusing the same DL next frame must not retain message ownership.
    cur = start;
    MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = cur;
    aero_ws_hud_scan_begin(ram, NULL);
    aero_ws_message_end(ram, cur); // early return without a matching begin
    rect(&cur, 200, 112, 14);
    MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = cur;
    aero_ws_hud_frame_end(ram, NULL);
    assert((gpr)MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) == cur + 80);
    assert(s_message_count == 0);

    // Unclosed or over-capacity metadata must leave both passes inactive.
    for (int overflow = 0; overflow < 2; overflow++) {
        cur = start;
        MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = cur;
        aero_ws_hud_scan_begin(ram, NULL);
        for (int i = 0; i < (overflow ? AERO_WS_MAX_MESSAGES + 1 : 1); i++) {
            aero_ws_message_begin(ram, cur);
            rect(&cur, 200, 112, 14);
            if (overflow) aero_ws_message_end(ram, cur);
        }
        MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = cur;
        aero_ws_hud_frame_end(ram, NULL);
        assert((gpr)MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) == cur);
    }
    puts("HUD message retag assertions passed");
    return 0;
}
