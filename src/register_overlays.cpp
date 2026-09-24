#include "recomp_overlays.inl"
#include "aero_mod_patch_guards.inc"
#include "aero_register_overlays.h"

void register_overlays() {
    aero_register_overlays(
        {section_table, sizeof(section_table) / sizeof(section_table[0]), num_sections},
        {overlay_sections_by_index,
         sizeof(overlay_sections_by_index) / sizeof(overlay_sections_by_index[0])},
        protected_mod_functions);
}
