// Exercise the live entries and compare their guest outputs with the ROM.
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp.h"

#ifdef AERO_JAPAN_SUPPORT
int aero_japan = 1;
#define func_80007538 jp_func_80007980
#define func_8005A034 jp_func_8005A38C
#define func_8005A2D4 jp_func_8005A660
#define rom_func_80007538 rom_jp_func_80007980
#define rom_func_8005A034 rom_jp_func_8005A38C
#define rom_func_8005A2D4 rom_jp_func_8005A660
#define CAR_BASE 0x8013D030u
#define CAR_STRIDE 0x2094
#define RENDER_OFFSET(x) ((x) - 12)
#else
#define CAR_BASE 0x8013FFB0u
#define CAR_STRIDE 0x20A0
#define RENDER_OFFSET(x) (x)
#endif

recomp_func_t func_80007538, func_8005A034, func_8005A2D4;
recomp_func_t rom_func_80007538, rom_func_8005A034, rom_func_8005A2D4;
static int enabled;
int aero_force_full_lod_enabled(void) { return enabled; }
// The extracted car-LOD closure contains a jump-table switch whose default arm
// calls switch_error (N64Recomp cgenerator.cpp, emit_switch_error). librecomp
// defines that symbol, but this host test links no runtime, and the closure is
// regenerated whenever the ROM translation changes -- so the reference appears
// and disappears with the generated code, and Windows CI hit it as an undefined
// symbol. Keep the same failing contract here: an arm no fixture drives is a
// test bug, not a pass.
void switch_error(const char* func, uint32_t vram, uint32_t jtbl) {
    fprintf(stderr, "switch_error in %s at 0x%08X for jump table 0x%08X\n", func, vram, jtbl);
    abort();
}
_Alignas(8) static uint8_t ram[8 * 1024 * 1024];
static uint8_t* rdram = ram;
static const gpr car = (gpr)(int32_t)CAR_BASE;
static const gpr camera = (gpr)(int32_t)0x80500000u;
static gpr addr(unsigned value) {
#ifdef AERO_JAPAN_SUPPORT
    // Fixture addresses from Rev A's actual car selectors, not port helpers.
    switch (value) {
    case 0x8013FC91: value = 0x8013CD11; break;
    case 0x8013FF90: value = 0x8013D010; break;
    case 0x8016C450: value = 0x80169470; break;
    case 0x800951B4: value = 0x80092C94; break;
    case 0x80098618: value = 0x80095E88; break;
    case 0x8008F820: value = 0x8008D780; break;
    case 0x8008F910: value = 0x8008D870; break;
    case 0x8008F728: value = 0x8008D688; break;
    case 0x8008F7A4: value = 0x8008D704; break;
    case 0x80098330: value = 0x80095BA0; break;
    case 0x800984A4: value = 0x80095D14; break;
    case 0x80097B20: value = 0x80095390; break;
    }
#endif
    return (gpr)(int32_t)value;
}
static void putf(gpr base, unsigned offset, float value) {
    int32_t bits; memcpy(&bits, &value, 4); MEM_W(offset, base) = bits;
}
static void call(recomp_func_t* fn, gpr arg) {
    recomp_context ctx = {0};
    ctx.r29 = addr(0x807FF000); ctx.r4 = arg;
    ctx.f_odd = &ctx.f0.u32h;
    fn(rdram, &ctx);
    assert(ctx.r29 == addr(0x807FF000));
}
static void setup(void) {
    memset(ram, 0, sizeof(ram));
    MEM_BU(0, addr(0x8013FC91)) = 1;
    MEM_B(0, addr(0x8013FF90)) = 4;
    MEM_W(0, addr(0x8016C450)) = 10;
    putf(addr(0x800951B4), 0, 750);
    putf(camera, 0xD8, 1); // camera looks along +Z
    for (unsigned i = 0; i < 10; ++i) {
        MEM_BU(i * 2, addr(0x80098618)) = i * 3;
        MEM_BU(i * 2 + 1, addr(0x80098618)) = 3;
        MEM_W(i * 24, addr(0x8008F820)) = 0x01003000 + i * 8;
        MEM_W(i * 24, addr(0x8008F910)) = 0x01004000 + i * 8;
    }
    for (unsigned i = 0; i < 31; ++i) {
        MEM_W(i * 4, addr(0x8008F728)) = 0x01001000 + i * 8;
        MEM_W(i * 4, addr(0x8008F7A4)) = 0x01002000 + i * 8;
    }
    // Craft 1's animated part uses transform +0x16 in the selected table.
    MEM_H(0x16, addr(0x80098330)) = 22;
    MEM_H(0x16, addr(0x800984A4)) = 33;
    MEM_H(0x22, addr(0x80098330)) = -17;
    MEM_H(0x22, addr(0x800984A4)) = -21;
    for (unsigned i = 0; i < 6; ++i)
        MEM_W(i * 8, addr(0x80097B20)) = 0x40468000; // guest double 45.0
}
static void visibility(float distance, float x, int visible, int low) {
    putf(car, RENDER_OFFSET(0x4D0), x); putf(car, RENDER_OFFSET(0x4D8), distance);
    call(func_80007538, camera);
    assert((MEM_W(0x1B8, camera) != 0) == visible);
#ifndef AERO_JAPAN_SUPPORT
    assert((MEM_W(0x1E0, camera) != 0) == visible);
#else
    // Rev A has only the primary node array; USA also publishes a second.
    assert(MEM_W(0x1E0, camera) == 0);
#endif
    assert(((MEM_BU(0, car) & 4) == 0) == visible);
    if (visible) assert((MEM_BU(0, car) & 1) == low);
}
static void model(int low) {
    call(func_8005A034, car);
    const unsigned part = MEM_BU(9, car) * 3;
    for (unsigned i = 0; i < 3; ++i)
        assert(MEM_W(RENDER_OFFSET(0x544) + i * 0xB8, car) ==
               MEM_W((part + i) * 4, addr(low ? 0x8008F7A4 : 0x8008F728)));
    assert(!(MEM_BU(1, car) & 0x80));
    assert(MEM_W(RENDER_OFFSET(0x4B0), car) == MEM_W(MEM_BU(9, car) * 24,
               addr(low ? 0x8008F910 : 0x8008F820)));
    // An already-correct mesh must not schedule another rebuild every frame.
    MEM_W(RENDER_OFFSET(0x4A8), car) = 99;
    call(func_8005A034, car);
    assert(MEM_W(RENDER_OFFSET(0x4A8), car) == 99);
}

static void compare_with_rom(recomp_func_t* live, recomp_func_t* reference, gpr arg) {
    // The routines own the car/camera records. Their caller's scratch stack
    // and volatile registers may differ between the two implementations.
    uint8_t* records[] = {ram + (uint32_t)(car - addr(0x80000000)),
                          ram + (uint32_t)(camera - addr(0x80000000))};
    const size_t sizes[] = {2 * CAR_STRIDE, 0x200};
    uint8_t before[2 * CAR_STRIDE + 0x200], expected[sizeof(before)];
    size_t offset = 0;
    for (unsigned i = 0; i < 2; ++i) {
        memcpy(before + offset, records[i], sizes[i]);
        offset += sizes[i];
    }
    call(reference, arg);
    offset = 0;
    for (unsigned i = 0; i < 2; ++i) {
        memcpy(expected + offset, records[i], sizes[i]);
        memcpy(records[i], before + offset, sizes[i]);
        offset += sizes[i];
    }
    call(live, arg);
    offset = 0;
    for (unsigned i = 0; i < 2; ++i) {
        assert(memcmp(records[i], expected + offset, sizes[i]) == 0);
        offset += sizes[i];
    }
}

static void stock_parity(void) {
    setup();
    enabled = 0;
    MEM_BU(0, addr(0x8013FC91)) = 2;
    MEM_W(4, camera) = (int32_t)car;
    MEM_BU(CAR_STRIDE + 0xB, car) = 3;
    MEM_W(4, addr(0x8016C450)) = 11;
    putf(car + CAR_STRIDE, RENDER_OFFSET(0x4D0), 160);
    putf(car + CAR_STRIDE, RENDER_OFFSET(0x4D8), 100);
    const float distances[] = {9, 10, 149, 150, 151, 750, 751, -100};
    for (unsigned mode = 4; mode <= 5; ++mode) {
        MEM_B(0, addr(0x8013FF90)) = mode;
        for (unsigned i = 0; i < sizeof(distances) / sizeof(distances[0]); ++i) {
            putf(car, RENDER_OFFSET(0x4D8), distances[i]);
            compare_with_rom(func_80007538, rom_func_80007538, camera);
        }
    }
    // Both ROMs have the same craft-to-part layout.
    const uint8_t craft_parts[][2] = {
        {29, 2}, {0, 3}, {3, 3}, {6, 5}, {11, 3},
        {14, 3}, {17, 3}, {20, 3}, {23, 3}, {26, 3},
    };
    for (unsigned craft = 0; craft < 10; ++craft) {
        MEM_BU(craft * 2, addr(0x80098618)) = craft_parts[craft][0];
        MEM_BU(craft * 2 + 1, addr(0x80098618)) = craft_parts[craft][1];
    }
    for (unsigned part = 0; part < 31; ++part) {
        for (unsigned field = 0; field < 6; ++field) {
            MEM_H(part * 12 + field * 2, addr(0x80098330)) = (int)field - (int)part;
            MEM_H(part * 12 + field * 2, addr(0x800984A4)) = (int)part - (int)field;
        }
    }
    for (unsigned mode = 4; mode <= 5; ++mode) {
        MEM_B(0, addr(0x8013FF90)) = mode;
        for (unsigned craft = 0; craft <= 10; ++craft) {
            MEM_BU(9, car) = craft;
            MEM_BU(0xA, car) = craft % 6;
            for (unsigned distant = 0; distant < 2; ++distant) {
                MEM_BU(0, car) = distant;
                if (craft < 10) {
                    MEM_BU(1, car) = 0xA5;
                    compare_with_rom(func_8005A034, rom_func_8005A034, car);
                    compare_with_rom(func_8005A034, rom_func_8005A034, car);
                }
                for (int sign = -1; sign <= 1; ++sign) {
                    putf(car, 0x5C, 19.25f * sign);
                    putf(car, 0x64, 0.5f * sign);
                    for (unsigned node = 0; node < 4; ++node) {
                        MEM_W(RENDER_OFFSET(0x5AC) + node * 0xB8, car) = 0x12345678;
                    }
                    compare_with_rom(func_8005A2D4, rom_func_8005A2D4, car);
                }
            }
        }
    }
}

int main(void) {
    stock_parity();
    setup();
    enabled = 0;
    visibility(9, 0, 0, 0); visibility(10, 0, 1, 0);
    visibility(149, 0, 1, 0); visibility(150, 0, 1, 0);
    visibility(151, 0, 1, 1); model(1);
    visibility(750, 0, 1, 1); visibility(751, 0, 0, 0);
    enabled = 1;
    model(0); // toggle with an already-installed low model, before camera update
    visibility(151, 0, 1, 0); visibility(751, 0, 1, 0);
    visibility(50000, 0, 1, 0);
    visibility(9, 0, 0, 0); visibility(10, 0, 1, 0);
    visibility(-100, 0, 0, 0); visibility(100, 200, 0, 0);
    visibility(100, 170, 1, 0);
    // Same car seen by another camera: its own direction test still applies.
    putf(camera, 0xD8, -1);
    visibility(100, 0, 0, 0);
    putf(camera, 0xD8, 1);
    enabled = 0;
    visibility(151, 0, 1, 1); model(1);
    visibility(751, 0, 0, 0);

    // Mode 5 bypasses distance LOD and has its own low-detail selector.
    MEM_B(0, addr(0x8013FF90)) = 5;
    for (unsigned craft = 0; craft < 10; ++craft) {
        MEM_BU(9, car) = craft;
        MEM_BU(0, car) = 0;
        enabled = 0; model(1);
        enabled = 1; model(0);
        enabled = 0; model(1); // no camera transition / stale high mesh
    }
    MEM_BU(9, car) = 1;
    putf(car, 0x5C, -20); putf(car, 0x64, -20);
    enabled = 1; model(0); call(func_8005A2D4, car);
    const int32_t forced_transform = MEM_W(RENDER_OFFSET(0x59C), car);
    enabled = 0; model(1); call(func_8005A2D4, car);
    assert(MEM_W(RENDER_OFFSET(0x59C), car) != forced_transform);
    MEM_B(0, addr(0x8013FF90)) = 4;
    MEM_BU(0, car) &= ~1u;
    model(0); call(func_8005A2D4, car);
    assert(MEM_W(RENDER_OFFSET(0x59C), car) == forced_transform);

    // Invalid addresses/indices fail without touching the car's flag bytes.
    enabled = 1;
    MEM_BU(0, car) = 1; MEM_BU(1, car) = 0;
    call(func_8005A034, addr(0x1000));
    call(func_8005A034, addr(0x807FFFFC));
    call(func_8005A034, car + 1);
    MEM_BU(9, car) = 10; call(func_8005A034, car);
    MEM_BU(9, car) = 1; MEM_BU(2, addr(0x80098618)) = 31;
    call(func_8005A034, car);
    assert(MEM_BU(0, car) == 1 && MEM_BU(1, car) == 0);

    setup();
    MEM_BU(0, addr(0x8013FC91)) = 2;
    MEM_BU((CAR_STRIDE + 11), car) = 1;
    MEM_W(4, addr(0x8016C450)) = 11;
    putf(car, RENDER_OFFSET(0x4D8), 100); putf(car + CAR_STRIDE, RENDER_OFFSET(0x4D8), 1000);
    call(func_80007538, camera);
    assert(MEM_W(0x1B8, camera) == (int32_t)(car + RENDER_OFFSET(0x498)));
    assert(MEM_W(0x1BC, camera) == (int32_t)(car + CAR_STRIDE + RENDER_OFFSET(0x498)));
    assert(!(MEM_BU(CAR_STRIDE, car) & 5));
    puts("PASS car_lod: stock ROM parity, visibility, model transitions and forced detail");
    return 0;
}
