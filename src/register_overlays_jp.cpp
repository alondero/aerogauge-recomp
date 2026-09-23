// Keep the two generated overlay tables in separate translation units.
#define recomp_entrypoint recomp_entrypoint_jp
#include "../RecompiledFuncsJP/recomp_overlays.inl"
#undef recomp_entrypoint

#include "librecomp/overlays.hpp"

extern "C" void aero_crash_register_code_ptrs(
    uint32_t ram_addr, uint32_t size, FuncEntry* funcs, size_t num_funcs);

void register_overlays_jp() {
    for (const auto& section : section_table) {
        aero_crash_register_code_ptrs(section.ram_addr, section.size,
                                     section.funcs, section.num_funcs);
    }
    recomp::overlays::register_overlays(
        {section_table, sizeof(section_table) / sizeof(section_table[0]), num_sections},
        {overlay_sections_by_index,
         sizeof(overlay_sections_by_index) / sizeof(overlay_sections_by_index[0])});
}
