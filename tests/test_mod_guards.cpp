#undef NDEBUG
#include <cassert>
#include <cstring>
#include "aero_register_overlays.h"

namespace usa {
#include "aero_mod_patch_guards.inc"
}
#ifdef AERO_JAPAN_SUPPORT
namespace japan {
#include "aero_mod_patch_guards_jp.inc"
}
#endif

static SectionTableEntry* registered_patches;
static size_t registered_patch_count;
static size_t crash_functions;
static bool overlays_registered;

extern "C" void aero_crash_register_code_ptrs(uint32_t, uint32_t, FuncEntry*, size_t count) {
    crash_functions += count;
}
namespace recomp::overlays {
void register_overlays(const overlay_section_table_data_t& sections, const overlays_by_index_t&) {
    assert(sections.num_code_sections == 1);
    overlays_registered = true;
}
void register_patches(const char*, size_t size, SectionTableEntry* sections, size_t count) {
    assert(overlays_registered && size == 0);
    registered_patches = sections;
    registered_patch_count = count;
}
}

int main(int argc, char** argv) {
    std::span<const uint32_t> guards = usa::protected_mod_functions;
    uint32_t hook_address = 0x80007538; // car LOD hook in the USA profile
#ifdef AERO_JAPAN_SUPPORT
    if (argc > 1 && std::strcmp(argv[1], "jp") == 0) {
        guards = japan::protected_mod_functions;
        hook_address = 0x80007980; // corresponding Japanese car LOD hook
    }
#endif
    constexpr uint32_t base = 0x80000400;
    FuncEntry funcs[] = {
        {.func = nullptr, .offset = hook_address - base, .rom_size = 64},
        {.func = nullptr, .offset = 0x123456, .rom_size = 64},
    };
    SectionTableEntry sections[] = {
        {.rom_addr = 0x1000, .ram_addr = base, .size = 0x200000,
         .funcs = funcs, .num_funcs = 2},
    };
    int overlays[] = {-1};
    aero_register_overlays({sections, 1, 1}, {overlays, 1}, guards);
    assert(crash_functions == 2);
    assert(funcs[0].rom_size == 0); // runtime must reject regenerating a port hook
    assert(funcs[1].rom_size == 64); // ordinary functions remain moddable
    assert(registered_patch_count == 1);
    assert(registered_patches[0].num_funcs == 1);
    assert(registered_patches[0].funcs[0].offset == hook_address - base);
    assert(registered_patches[0].ram_addr == base);
}
