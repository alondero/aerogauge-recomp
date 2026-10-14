#pragma once

#include <cstdint>

namespace aero::full_track {

struct SectionOverride {
    uint8_t track;
    uint8_t zone;
    uint32_t display_list;
};

inline constexpr SectionOverride kPvsGatedExceptions[] = {
    // Bikini Island tunnel rock wedge; verified from the zone-13 section table.
    {1, 13, 0x803903B8u},
    // Bikini Island rotating-tunnel cap; zone 20's PVS excludes zone 23.
    {1, 23, 0x80396070u},
    // Chinatown early-road wall tops overlap the later elevated road.
    {2, 4, 0x803C8220u},  // USA
    {2, 4, 0x803C82B0u},  // Japan Rev A
    // Chinatown Jam's corresponding wall tops use a different mesh and zone.
    {4, 9, 0x803CD540u},  // USA
    {4, 9, 0x803CD5D0u},  // Japan Rev A
};

// Most enclosed course shells advertise themselves through hw4 bit 0x10. The
// exact unflagged exceptions above also need the original visibility window:
// always-visible buckets can obstruct or intersect other parts of the course.
constexpr bool pvs_gated_section(uint8_t track, uint8_t zone,
                                 uint32_t display_list, uint16_t hw4) {
    if ((hw4 & 0x10u) != 0) return true;
    for (const SectionOverride& ex : kPvsGatedExceptions) {
        if (ex.track == track && ex.zone == zone &&
            ex.display_list == display_list) return true;
    }
    return false;
}

} // namespace aero::full_track
