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
    constexpr uint32_t usa_entry_replacements[] = {0x80007538, 0x8005A034, 0x8005A2D4, 0x800227E4};
    constexpr uint32_t usa_intro[] = {0x8000D708, 0x8000D97C, 0x800191FC, 0x8001F998};
    std::span<const uint32_t, 4> entry_replacements = usa_entry_replacements;
    std::span<const uint32_t, 4> intro_entries = usa_intro;
#ifdef AERO_JAPAN_SUPPORT
    if (argc > 1 && std::strcmp(argv[1], "jp") == 0) {
        guards = japan::protected_mod_functions;
        static constexpr uint32_t jp_entry_replacements[] = {0x80007980, 0x8005A38C, 0x8005A660, 0x80023238};
        static constexpr uint32_t jp_intro[] = {0x8000DDA0, 0x8000E144, 0x80019C94, 0x8002025C};
        entry_replacements = jp_entry_replacements;
        intro_entries = jp_intro;
    }
#endif
    for (uint32_t entry : intro_entries) {
        assert(std::binary_search(guards.begin(), guards.end(), entry));
    }
    constexpr uint32_t base = 0x80000400;
    FuncEntry funcs[] = {
        {.func = nullptr, .offset = entry_replacements[0] - base, .rom_size = 64},
        {.func = nullptr, .offset = entry_replacements[1] - base, .rom_size = 64},
        {.func = nullptr, .offset = entry_replacements[2] - base, .rom_size = 64},
        {.func = nullptr, .offset = entry_replacements[3] - base, .rom_size = 64},
        {.func = nullptr, .offset = 0x123456, .rom_size = 64},
    };
    SectionTableEntry sections[] = {
        {.rom_addr = 0x1000, .ram_addr = base, .size = 0x200000,
         .funcs = funcs, .num_funcs = 5},
    };
    int overlays[] = {-1};
    aero_register_overlays({sections, 1, 1}, {overlays, 1}, guards);
    assert(crash_functions == 5);
    assert(funcs[4].rom_size == 64); // ordinary functions remain moddable
    assert(registered_patch_count == 1);
    assert(registered_patches[0].num_funcs == entry_replacements.size());
    for (size_t i = 0; i < entry_replacements.size(); ++i) {
        assert(funcs[i].rom_size == 0); // reject regenerating an entry replacement
        assert(registered_patches[0].funcs[i].offset == entry_replacements[i] - base);
    }
    assert(registered_patches[0].ram_addr == base);
}
