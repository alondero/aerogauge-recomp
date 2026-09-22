// Exercise generated ROM code, not a duplicate of its decision logic.
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "recomp.h"

void func_80007538(uint8_t*, recomp_context*);
void func_8005A034(uint8_t*, recomp_context*);
void func_8005A2D4(uint8_t*, recomp_context*);
void aero_car_lod_model(uint8_t*, recomp_context*);
static int enabled;
int aero_force_full_lod_enabled(void) { return enabled; }
_Alignas(8) static uint8_t ram[8 * 1024 * 1024];
static uint8_t* rdram = ram;
static const gpr car = (gpr)(int32_t)0x8013FFB0u;
static const gpr camera = (gpr)(int32_t)0x80500000u;
static gpr addr(unsigned value) { return (gpr)(int32_t)value; }
static void putf(gpr base, unsigned offset, float value) {
    int32_t bits; memcpy(&bits, &value, 4); MEM_W(offset, base) = bits;
}
static void call(void (*fn)(uint8_t*, recomp_context*), gpr arg) {
    recomp_context ctx = {0};
    ctx.r29 = addr(0x807FF000); ctx.r4 = arg;
    ctx.f_odd = &ctx.f0.u32h;
    fn(rdram, &ctx);
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
}
static void visibility(float distance, float x, int visible, int low) {
    putf(car, 0x4D0, x); putf(car, 0x4D8, distance);
    call(func_80007538, camera);
    assert((MEM_W(0x1B8, camera) != 0) == visible);
    assert((MEM_W(0x1E0, camera) != 0) == visible);
    assert(((MEM_BU(0, car) & 4) == 0) == visible);
    if (visible) assert((MEM_BU(0, car) & 1) == low);
}
static void model(int low) {
    call(func_8005A034, car);
    const unsigned part = MEM_BU(9, car) * 3;
    for (unsigned i = 0; i < 3; ++i)
        assert(MEM_W(0x544 + i * 0xB8, car) ==
               MEM_W((part + i) * 4, addr(low ? 0x8008F7A4 : 0x8008F728)));
    assert(!(MEM_BU(1, car) & 0x80)); // rebuild consumed by ROM
    assert(MEM_W(0x4B0, car) == MEM_W(MEM_BU(9, car) * 24,
               addr(low ? 0x8008F910 : 0x8008F820)));
}
int main(void) {
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
    const int32_t forced_transform = MEM_W(0x59C, car);
    enabled = 0; model(1); call(func_8005A2D4, car);
    assert(MEM_W(0x59C, car) != forced_transform);
    MEM_B(0, addr(0x8013FF90)) = 4;
    MEM_BU(0, car) &= ~1u;
    model(0); call(func_8005A2D4, car);
    assert(MEM_W(0x59C, car) == forced_transform);

    // Invalid addresses/indices fail without touching the car's flag bytes.
    enabled = 1;
    MEM_BU(0, car) = 1; MEM_BU(1, car) = 0;
    call(aero_car_lod_model, addr(0x1000));
    call(aero_car_lod_model, addr(0x807FFFFC));
    call(aero_car_lod_model, car + 1);
    MEM_BU(9, car) = 10; call(aero_car_lod_model, car);
    MEM_BU(9, car) = 1; MEM_BU(2, addr(0x80098618)) = 31;
    call(aero_car_lod_model, car);
    assert(MEM_BU(0, car) == 1 && MEM_BU(1, car) == 0);

    setup();
    MEM_BU(0, addr(0x8013FC91)) = 2;
    MEM_BU(0x20AB, car) = 1;
    MEM_W(4, addr(0x8016C450)) = 11;
    putf(car, 0x4D8, 100); putf(car + 0x20A0, 0x4D8, 1000);
    call(func_80007538, camera);
    assert(MEM_W(0x1B8, camera) == (int32_t)(car + 0x498));
    assert(MEM_W(0x1BC, camera) == (int32_t)(car + 0x20A0 + 0x498));
    assert(!(MEM_BU(0x20A0, car) & 5));
    puts("PASS car_lod: ROM visibility, model transitions, mode override and animation");
    return 0;
}
