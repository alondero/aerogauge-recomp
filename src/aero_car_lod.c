#include <string.h>
#include "recomp.h"
#include "aero_region.h"

int aero_force_full_lod_enabled(void);

recomp_func_t func_80024240, func_800254F0, func_80020B68;
#ifdef AERO_JAPAN_SUPPORT
recomp_func_t jp_func_80024DE0, jp_func_80026130, jp_func_8002152C;
#endif

enum {
    DISTANT_DETAIL = 0x01,
    CAMERA_CAR_CULLED = 0x02,
    CULLED = 0x04,
    REBUILD_MODEL = 0x80,
    DISTANT_DETAIL_MODE = 5,
    PART_STRIDE = 0xB8,
    PART_MESH = 0xAC,
};

// The game thread owns these camera and car records for the duration of the
// call. ROM tables and record offsets are regional; MEM helpers use guest
// byte order. The translated callees use temporary space on the guest stack.
#define GUEST(us, jp) ((gpr)(int32_t)AERO_ADDR(us, jp))
#define RENDER_OFFSET(us) AERO_ADDR(us, (us) - 12)

static float read_float(uint8_t* rdram, gpr base, unsigned offset) {
    const int32_t bits = MEM_W(offset, base);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void write_float(uint8_t* rdram, gpr base, unsigned offset, float value) {
    int32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    MEM_W(offset, base) = bits;
}

static double read_double(uint8_t* rdram, gpr base, unsigned offset) {
    const uint64_t bits = ((uint64_t)(uint32_t)MEM_W(offset, base) << 32) |
                          (uint32_t)MEM_W(offset + 4, base);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int distant_mode(uint8_t* rdram) {
    return MEM_B(0, GUEST(0x8013FF90, 0x8013D010)) == DISTANT_DETAIL_MODE;
}

static int use_distant_detail(uint8_t* rdram, gpr car, int force_full) {
    return !force_full && ((MEM_BU(0, car) & DISTANT_DETAIL) || distant_mode(rdram));
}

static void set_distant_detail(uint8_t* rdram, gpr car, int distant) {
    const unsigned old_flags = MEM_BU(0, car);
    const unsigned flags = (old_flags & ~DISTANT_DETAIL) | (distant ? DISTANT_DETAIL : 0);
    if (flags != old_flags) {
        MEM_BU(0, car) = flags;
        MEM_BU(1, car) |= REBUILD_MODEL;
    }
}

static float normalize(uint8_t* rdram, recomp_context* ctx, gpr vector) {
    ctx->r4 = vector;
    AERO_BRANCH(func_80024240, jp_func_80024DE0)(rdram, ctx);
    return ctx->f0.fl;
}

void aero_update_car_visibility(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r4;
    const gpr car_count = GUEST(0x8013FC91, 0x8013CD11);
    if (!MEM_BU(0, car_count)) return;

    const int force_full = aero_force_full_lod_enabled();
    const float far_distance = read_float(rdram, GUEST(0x800951B4, 0x80092C94), 0);
    const gpr stack = ctx->r29;
    ctx->r29 = SUB32(stack, AERO_BRANCH(0xA8, 0xA0));
    const gpr to_car = ADD32(ctx->r29, AERO_BRANCH(0x90, 0x88));
    const gpr forward = SUB32(to_car, 12);
    gpr car = GUEST(0x8013FFB0, 0x8013D030);

    for (unsigned i = 0; i < MEM_BU(0, car_count);
         ++i, car = ADD32(car, AERO_BRANCH(0x20A0, 0x2094))) {
        for (unsigned axis = 0; axis < 12; axis += 4) {
            write_float(rdram, to_car, axis,
                read_float(rdram, car, RENDER_OFFSET(0x4D0) + axis) -
                read_float(rdram, camera, 0xC4 + axis));
        }
        const float distance = normalize(rdram, ctx, to_car);
        for (unsigned axis = 0; axis < 12; axis += 4) {
            write_float(rdram, forward, axis,
                read_float(rdram, camera, 0xD0 + axis) -
                read_float(rdram, camera, 0xC4 + axis));
        }
        normalize(rdram, ctx, forward);
        int culled = (!force_full && far_distance < distance) || distance < 10.0f;
        if (!culled) {
            const float dot = (read_float(rdram, to_car, 0) * read_float(rdram, forward, 0) +
                               read_float(rdram, to_car, 4) * read_float(rdram, forward, 4)) +
                               read_float(rdram, to_car, 8) * read_float(rdram, forward, 8);
            culled = dot < 0.5f;
        }
        gpr primary_node = 0;
        if (culled) {
            // Rev A uses bit 1 when the rejected car belongs to this camera.
            MEM_BU(0, car) |= (AERO_IS_JP && MEM_W(4, camera) == (int32_t)car)
                ? CAMERA_CAR_CULLED : CULLED;
        } else {
            MEM_BU(0, car) &= ~(AERO_IS_JP ? CULLED | CAMERA_CAR_CULLED : CULLED);
            if (!distant_mode(rdram)) {
                set_distant_detail(rdram, car, !force_full && 150.0f < distance);
            }
            primary_node = ADD32(car, RENDER_OFFSET(0x498));
        }

        MEM_W(0x1B8 + MEM_BU(0xB, car) * 4, camera) = (int32_t)primary_node;
        if (!AERO_IS_JP) {
            const unsigned index = (uint32_t)MEM_W(i * 4, GUEST(0x8016C450, 0));
            MEM_W(0x1B8 + index * 4, camera) = culled ? 0 : (int32_t)ADD32(car, 0x1E78);
        }
    }
    ctx->r29 = stack;
}

void aero_rebuild_car_model(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t address = (uint32_t)ctx->r4;
    if ((address & 3) || address < 0x80000400u ||
        address > 0x80700000u - AERO_BRANCH(0x20A0u, 0x2094u)) return;

    const gpr car = (gpr)(int32_t)address;
    const unsigned craft = MEM_BU(9, car);
    if (craft >= 10) return;
    const gpr craft_parts = GUEST(0x80098618, 0x80095E88);
    const unsigned first_part = MEM_BU(craft * 2, craft_parts);
    const unsigned part_count = MEM_BU(craft * 2 + 1, craft_parts);
    if (first_part >= 31 || part_count > 31 - first_part) return;

    const int force_full = aero_force_full_lod_enabled();
    if (force_full) set_distant_detail(rdram, car, 0);
    const int distant = use_distant_detail(rdram, car, force_full);
    const gpr meshes = distant ? GUEST(0x8008F7A4, 0x8008D704) : GUEST(0x8008F728, 0x8008D688);
    const gpr transforms = distant ? GUEST(0x800984A4, 0x80095D14) : GUEST(0x80098330, 0x80095BA0);
    const gpr materials = distant ? GUEST(0x8008F910, 0x8008D870) : GUEST(0x8008F820, 0x8008D780);
    const gpr root = ADD32(car, RENDER_OFFSET(0x498));
    // An installed mesh may outlive a settings change or a save-state restore.
    // Use the same selected table for the rebuild gate and the part writes.
    if (!(MEM_BU(1, car) & REBUILD_MODEL) &&
        (!part_count || MEM_W(PART_MESH, root) == MEM_W(first_part * 4, meshes))) return;

    for (unsigned i = 0; i < part_count; ++i) {
        const unsigned part = first_part + i;
        const gpr node = ADD32(root, i * PART_STRIDE);
        for (unsigned field = 0; field < 6; ++field) {
            write_float(rdram, node, 0x38 + field * 4,
                        (float)MEM_H(part * 12 + field * 2, transforms));
        }
        MEM_W(PART_MESH, node) = MEM_W(part * 4, meshes);
        if (i == 0) {
            MEM_W(0xC, node) = 0;
            MEM_W(0x10, node) = 3;
            MEM_H(0x14, node) = 0;
            MEM_H(0x16, node) = 0;
            const int material_slot = MEM_B(0xA, car);
            MEM_W(0x18, node) = MEM_W((int)craft * 24 + material_slot * 4, materials);
            MEM_W(0x1C, node) = 1;
            MEM_BU(0x20, node) = 0;
            MEM_BU(0x21, node) = 0;
        }
    }
    MEM_BU(1, car) &= ~REBUILD_MODEL;
}

// USA 8005A2D4 / Rev A 8005A660 take one car pointer in a0 and return
// through memory. First the 254F0/26130 helper steps the float at +0x64
// toward +0x5C with a divisor of 5. The craft byte selects writes at
// +0x59C/+0x654 (and +0x70C/+0x7C4 for craft 3). Rev A shifts these
// destinations and the matrix inputs by -12. The other car fields retain
// their ROM offsets until their meaning is established.
void aero_update_car_animation(uint8_t* rdram, recomp_context* ctx) {
    const gpr car = ctx->r4;
    const gpr stack = ctx->r29;
    ctx->r29 = SUB32(stack, 0x18);
    ctx->r4 = ADD32(car, 0x64);
    ctx->r5 = (gpr)MEM_W(0x5C, car);
    ctx->r6 = 5;
    AERO_BRANCH(func_800254F0, jp_func_80026130)(rdram, ctx);

    const unsigned craft = MEM_BU(9, car);
    const float amount = -read_float(rdram, car, 0x64) / 20.0f;
    const unsigned first = RENDER_OFFSET(0x59C);
    const unsigned second = first + PART_STRIDE;
    const gpr scales = GUEST(0x80097B20, 0x80095390);
    switch (craft) {
    case 0:
        write_float(rdram, car, first, -amount * 45.0f);
        break;
    case 1: {
        const int distant = use_distant_detail(rdram, car, aero_force_full_lod_enabled());
        const gpr transforms = distant ? GUEST(0x800984A4, 0x80095D14) : GUEST(0x80098330, 0x80095BA0);
        if (amount > 0.0f) {
            write_float(rdram, car, first,
                        (float)(MEM_H(0x16, transforms) + amount * read_double(rdram, scales, 0)));
        } else if (amount < 0.0f) {
            write_float(rdram, car, second,
                        (float)(MEM_H(0x22, transforms) + amount * read_double(rdram, scales, 8)));
        }
        break;
    }
    case 2:
        if (amount > 0.0f) {
            write_float(rdram, car, first, (float)(amount * read_double(rdram, scales, 16)));
        } else if (amount < 0.0f) {
            write_float(rdram, car, second, (float)(amount * read_double(rdram, scales, 24)));
        }
        break;
    case 3:
        if (amount > 0.0f) {
            write_float(rdram, car, first, (float)(amount * read_double(rdram, scales, 32)));
            write_float(rdram, car, second, (float)(amount * 25.0));
        } else if (amount < 0.0f) {
            write_float(rdram, car, first + 2 * PART_STRIDE,
                        (float)(amount * read_double(rdram, scales, 40)));
            write_float(rdram, car, second + 2 * PART_STRIDE, (float)(amount * 25.0));
        }
        break;
    case 4:
        if (amount > 0.0f) write_float(rdram, car, first, (float)(amount * 15.0));
        if (amount < 0.0f) write_float(rdram, car, second, (float)(amount * 15.0));
        break;
    case 5: case 6: case 7: case 8: case 9:
        if (amount > 0.0f) write_float(rdram, car, first, amount * 40.0f);
        if (amount < 0.0f) write_float(rdram, car, second, amount * 40.0f);
        break;
    }

    // 20B68/2152C reads each part's float inputs and writes its matrix in
    // place. Both common calls happen even for an out-of-range craft byte.
    const unsigned matrix_count = craft == 3 ? 4 : 2;
    for (unsigned i = 0; i < matrix_count; ++i) {
        ctx->r4 = ADD32(car, RENDER_OFFSET(0x550) + i * PART_STRIDE);
        AERO_BRANCH(func_80020B68, jp_func_8002152C)(rdram, ctx);
    }
    ctx->r29 = stack;
}
