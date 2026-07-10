// Hand-provided no-op symbols for this ROM's libultra CP0/kernel helpers that the pivot routes
// AWAY from recompilation (epic #54, phase 3). These functions are named canonically in
// recomp/gen_syms_toml.py (LIBULTRA_NAMES) so N64Recomp marks them `ignored` (symbol_lists.cpp)
// and emits no body.
//
// N64Recomp renames BOTH reimplemented AND ignored functions to `<name>_recomp` at the call site
// (main.cpp:430-440); only `reimplemented` names also get a librecomp native. So an `ignored`
// helper's callers emit `<name>_recomp(...)` with NO definition anywhere -> we must supply it here
// under that exact suffix, rather than forking the vendored submodule to add a native.
//
// __osSetSR: the ROM's `mtc0 $a0,Status; jr $ra` (func_8007D260, verified from bytes). ultramodern
// REPLACES the CP0/interrupt kernel with native threads, so writing N64 Status bits is correctly a
// no-op (this is exactly why librecomp's cop0_status_write aborts on non-FR Status writes). FR-mode
// float addressing is set up by the runtime itself, so dropping the write is safe -- matches how
// drmario64/Zelda64Recomp leave __osSetSR in ignored_funcs.

#include "recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void __osSetSR_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

// TODO(aerogauge): the Lamborghini port carried a hand-translated __osViInit_recomp here
// (that ROM's private VI-manager globals). AeroGauge's VI init has not been mapped yet;
// re-derive it from this ROM's bytes before routing __osViInit to `ignored`.

// TODO(aerogauge): the Lamborghini port carried a fully-built SI controller-read bridge
// here (func_8007F780) that wired aero_joybus_answer (PIF joybus frame emulator) + a
// 32 KB controller-pak image into that ROM's hand-rolled SI kernel. All of it was keyed
// to Lambo function / global addresses (D_8011C640/680/681/6D0, the SI bridge at
// func_8007F780). Dropped from the base stack because nothing in AeroGauge calls those
// Lambo names. The PIF-level joybus primitives (data CRC, pak image format, frame
// dispatch table) ARE generic and worth preserving; re-add them once AeroGauge's SI
// read path is identified.
