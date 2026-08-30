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
};

// Most enclosed course shells advertise themselves through hw4 bit 0x10. Bikini
// Island has one authoring exception: zone 13's DL 0x803903B8 is a large rock
// wedge outside the tunnel. The original PVS hides it from zone 15, but merging
// it into the always-visible hw6=2 bucket makes it cut across the tunnel road.
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
