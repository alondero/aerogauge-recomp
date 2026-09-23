// This TU is the only place that includes the generated recomp_overlays.inl
// (it defines section_table[] / overlay_sections_by_index[] as file-local
// statics, so they can't be referenced from anywhere else).
//
// Feed each section's FuncEntry pointers to the crash module before librecomp
// registration, so the native-PC to N64-VRAM map is ready before the game thread
// starts running recompiled functions.

#include "recomp_overlays.inl"

#include "librecomp/overlays.hpp"
#include "librecomp/sections.h"
#include <algorithm>
#include <iterator>
#include <vector>
#include "aero_mod_patch_guards.inc"

#ifndef ARRLEN
#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

extern "C" void aero_crash_register_code_ptrs(
    uint32_t ram_addr, uint32_t size, FuncEntry* funcs, size_t num_funcs);

void register_overlays() {
    // Runtime hook regeneration reads the original ROM, which does not include
    // the port's injected hooks, stubs, or instruction fixes. Register those
    // entries as base patches so ordinary mod replacements report a conflict.
    // This metadata remains valid for the runtime lifetime and does not write
    // guest memory.
    static std::vector<std::vector<FuncEntry>> patched_functions(ARRLEN(section_table));
    static std::vector<SectionTableEntry> patched_sections;
    for (size_t i = 0; i < ARRLEN(section_table); i++) {
        const SectionTableEntry& sec = section_table[i];
        aero_crash_register_code_ptrs(sec.ram_addr, sec.size, sec.funcs, sec.num_funcs);
        for (size_t j = 0; j < sec.num_funcs; ++j) {
            auto& function = sec.funcs[j];
            if (std::binary_search(std::begin(protected_mod_functions),
                                   std::end(protected_mod_functions),
                                   sec.ram_addr + function.offset)) {
                function.rom_size = 0;
                patched_functions[i].push_back(function);
            }
        }
        if (!patched_functions[i].empty()) {
            auto patched = sec;
            patched.funcs = patched_functions[i].data();
            patched.num_funcs = patched_functions[i].size();
            patched_sections.push_back(patched);
        }
    }

    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
    static constexpr char empty_patch_data = 0;
    recomp::overlays::register_patches(&empty_patch_data, 0,
        patched_sections.data(), patched_sections.size());
}
