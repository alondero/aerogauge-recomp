#ifndef AERO_SHADOW_DEPTH_H
#define AERO_SHADOW_DEPTH_H

#include <stdint.h>

#include "ultramodern/ultra64.h"

// Apply the narrow race-shadow depth-mode patch in this graphics task's root
// display list. The explicit RDRAM size keeps the scanner host-testable and
// makes every command read task- and allocation-bounded.
void aero_patch_racer_shadow_depth(uint8_t* rdram, uint32_t rdram_size,
                                   const OSTask* task);

#endif
