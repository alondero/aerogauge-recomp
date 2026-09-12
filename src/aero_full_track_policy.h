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
};

// Most enclosed course shells advertise themselves through hw4 bit 0x10. Bikini
// Island also has unflagged authoring exceptions: the zone-13 rock wedge and
// zone-23 rotating-tunnel cap. Keep these on the original visibility window;
// merging them into always-visible buckets obstructs other parts of the course.
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
