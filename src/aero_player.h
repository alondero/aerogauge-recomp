#ifndef AERO_PLAYER_H
#define AERO_PLAYER_H

#include "aero_region.h"
#include "recomp.h"

// Game-thread car records store a 32-bit, guest-endian input callback at +4.
// USA func_8005C750/8005C878 and JP 8005CCD0/8005CDF4 read ports 0/1,
// respectively, before mapping controls. AI and replay callbacks are excluded.
// The caller validates the car record's RDRAM bounds before this read.
static inline int aero_car_player(uint8_t* rdram, gpr car) {
    const uint32_t callback = (uint32_t)MEM_W(4, car);
    if (callback == AERO_ADDR(0x8005C750u, 0x8005CCD0u)) return 0;
    if (callback == AERO_ADDR(0x8005C878u, 0x8005CDF4u)) return 1;
    return -1;
}

#endif
