#ifndef AERO_REGISTER_OVERLAYS_H
#define AERO_REGISTER_OVERLAYS_H

#include "librecomp/overlays.hpp"
#include <algorithm>
#include <span>
#include <vector>

extern "C" void aero_crash_register_code_ptrs(
    uint32_t ram_addr, uint32_t size, FuncEntry* funcs, size_t num_funcs);

// Called exactly once, for the validated region, before runtime startup.
inline void aero_register_overlays(
    const recomp::overlays::overlay_section_table_data_t& sections,
    const recomp::overlays::overlays_by_index_t& overlays,
    std::span<const uint32_t> protected_mod_functions) {
    // Runtime hook regeneration reads the original ROM, which does not include
    // the port's injected hooks, stubs, or instruction fixes. Register those
    // entries as base patches so ordinary mod replacements report a conflict.
    // This metadata remains valid for the runtime lifetime and does not write
    // guest memory.
    static std::vector<std::vector<FuncEntry>> patched_functions(sections.num_code_sections);
    static std::vector<SectionTableEntry> patched_sections;
    for (size_t i = 0; i < sections.num_code_sections; i++) {
        const SectionTableEntry& sec = sections.code_sections[i];
        aero_crash_register_code_ptrs(sec.ram_addr, sec.size, sec.funcs, sec.num_funcs);
        for (size_t j = 0; j < sec.num_funcs; ++j) {
            // section_table points into generated vanilla tables. Mutate their
            // ROM-size metadata intentionally so librecomp rejects mod hooks
            // for port-patched functions; the copied entry below preserves it.
            auto& function = sec.funcs[j];
            if (std::binary_search(protected_mod_functions.begin(),
                                   protected_mod_functions.end(),
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

    recomp::overlays::register_overlays(sections, overlays);
    static constexpr char empty_patch_data = 0;
    recomp::overlays::register_patches(&empty_patch_data, 0,
        patched_sections.data(), patched_sections.size());
}

#endif
