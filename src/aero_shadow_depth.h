#ifndef AERO_SHADOW_DEPTH_H
#define AERO_SHADOW_DEPTH_H

#include <stdint.h>

#include "ultramodern/ultra64.h"

// Apply the narrow race-shadow depth-mode patch in this graphics task's root
// display list. The scan never reads before the task root and stays within the
// supplied RDRAM allocation; it stops at G_ENDDL or the command safety cap.
void aero_patch_racer_shadow_depth(uint8_t* rdram, uint32_t rdram_size,
                                   const OSTask* task);

#endif
