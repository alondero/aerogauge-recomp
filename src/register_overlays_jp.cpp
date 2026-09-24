// Keep regional generated tables in separate translation units.
#define recomp_entrypoint recomp_entrypoint_jp
#include "../RecompiledFuncsJP/recomp_overlays.inl"
#undef recomp_entrypoint
#include "aero_mod_patch_guards_jp.inc"
#include "aero_register_overlays.h"

void register_overlays_jp() {
    aero_register_overlays(
        {section_table, sizeof(section_table) / sizeof(section_table[0]), num_sections},
        {overlay_sections_by_index,
         sizeof(overlay_sections_by_index) / sizeof(overlay_sections_by_index[0])},
        protected_mod_functions);
}
