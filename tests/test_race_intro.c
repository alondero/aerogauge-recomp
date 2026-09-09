#undef NDEBUG
#include <assert.h>
#include "../src/aero_race_intro.c"
static float output_aspect;
uint32_t aero_ws_get_output_aspect_bits(void) {
    uint32_t bits;
    memcpy(&bits, &output_aspect, 4);
    return bits;
}
static uint8_t ram[8 * 1024 * 1024];
static void original_rect(uint8_t* rdram, gpr p, int x, int y, int width, int height) {
    intro_emit(rdram, &p, 0xe4000000u | (((x + width) * 4u & 4095) << 12) | ((y + height) * 4),
               ((x * 4u & 4095) << 12) | (y * 4));
    intro_emit(rdram, &p, 0xb4000000u, 0x00200040);
    intro_emit(rdram, &p, 0xb3000000u, 0x04000400);
    intro_emit(rdram, &p, 0xe7000000u, 0);
}
int main(void) {
    uint8_t* rdram = ram;
    recomp_context ctx = {0};
    ctx.r29 = (gpr)(int32_t)0x80300000;
    ctx.r4 = ctx.r29 + 0x34;
    const gpr start = (gpr)(int32_t)0x80200000;
    MEM_W(0, (gpr)(int32_t)0x8013FF80) = 5;
    const float aspects[] = {4.0f/3, 16.0f/9, 21.0f/9, 32.0f/9};
    for (unsigned i = 0; i < 4; i++) {
        output_aspect = aspects[i];
        MEM_W(0, ctx.r4) = start;
        aero_intro_fade_begin(rdram, &ctx);
        gpr rect = MEM_W(0, ctx.r4);
        original_rect(rdram, rect, 0, 8, 319, 223);
        MEM_W(0, ctx.r4) = rect + 32;
        aero_intro_fade_end(rdram, &ctx);
        if (!i) {
            assert((gpr)MEM_W(0, ctx.r4) == start + 32);
            assert((uint32_t)MEM_W(0, rect) >> 24 == 0xe4);
            continue;
        }
        int dx = (int)ceilf((120 * aspects[i] - 160) * 4);
        assert((int16_t)((uint32_t)MEM_W(8, rect) >> 16) == -dx);
        assert((int16_t)((uint32_t)MEM_W(12, rect) >> 16) == 1280 + dx);
        assert((MEM_W(8, rect) & 65535) == 0);
        assert((MEM_W(12, rect) & 65535) == 960);
        assert((uint32_t)MEM_W(16, rect) == 0x00200040);
        assert((uint32_t)MEM_W(20, rect) == 0x04000400);
        assert((gpr)MEM_W(0, ctx.r4) == start + 88); // bounded 56-byte growth
        assert((uint32_t)MEM_W(0, start) == ((RT64_HOOK_OPCODE << 24) | RT64_HOOK_MAGIC_NUMBER));
        assert((uint32_t)MEM_W(4, start) == ((RT64_HOOK_OP_ENABLE << 28) | RT64_EXTENDED_OPCODE));

        MEM_W(0, ctx.r4) = start;
        aero_intro_ticker_begin(rdram, &ctx);
        original_rect(rdram, rect, 16, 180, 287, 23);
        MEM_W(0x64, ctx.r29) = rect + 32;
        aero_intro_banner(rdram, &ctx);
        assert((int16_t)((uint32_t)MEM_W(8, rect) >> 16) == -dx);
        assert((int16_t)((uint32_t)MEM_W(12, rect) >> 16) == 1280 + dx);
        ctx.r6 = 230;
        aero_intro_ticker_origin(rdram, &ctx);
        MEM_W(0xe0, ctx.r29) = ctx.r4;
        int previous = 0;
        for (int letter = 0; letter < 3; letter++) {
            int x = 230 + letter * 10;
            original_rect(rdram, rect, x, 185, 10, 14);
            MEM_W(0, ctx.r4) = rect + 32;
            MEM_W(0xe4, ctx.r29) = x;
            ctx.r2 = 1;
            aero_intro_glyph(rdram, &ctx);
            int left = (int16_t)((uint32_t)MEM_W(8, rect) >> 16);
            int right = (int16_t)((uint32_t)MEM_W(12, rect) >> 16);
            assert(right - left == 40);
            if (letter) assert(left - previous == 40);
            else assert(abs(left - (1280 + dx)) <= 1);
            previous = left;
        }
        // Missing glyph must leave the previous emitted rectangle untouched.
        uint32_t saved = MEM_W(0, rect);
        ctx.r2 = 0;
        aero_intro_glyph(rdram, &ctx);
        assert((uint32_t)MEM_W(0, rect) == saved);
        // Later in the scroll, use the original signed argument, not its
        // wrapped 12-bit RDP representation. Adjacent letters still travel together.
        ctx.r6 = (gpr)(int32_t)-400;
        aero_intro_ticker_origin(rdram, &ctx);
        for (int letter = 0; letter < 2; letter++) {
            int x = -400 + letter * 10;
            original_rect(rdram, rect, x, 185, 10, 14);
            MEM_W(0xe4, ctx.r29) = x;
            ctx.r2 = 1;
            aero_intro_glyph(rdram, &ctx);
            int left = (int16_t)((uint32_t)MEM_W(8, rect) >> 16);
            assert(left < -dx);
            if (letter) assert(left - previous == 40);
            previous = left;
        }
        MEM_W(0x2c, ctx.r29) = rect + 32;
        aero_intro_ticker_end(rdram, &ctx);
        assert(ctx.r14 == (gpr)MEM_W(0x2c, ctx.r29));
        assert(ticker_extra == 0);
    }
    MEM_W(0, (gpr)(int32_t)0x8013FF80) = 4;
    assert(intro_extra(rdram) == 0);
    return 0;
}
