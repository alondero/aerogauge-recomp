#pragma once

#include <cstdint>

namespace aero::full_track {

constexpr int kBikiniIslandTrack = 1;
constexpr uint8_t kBikiniTunnelRockZone = 13;
constexpr uint32_t kBikiniTunnelRockEntry = 11;
constexpr uint32_t kBikiniTunnelRockDisplayList = 0x803903B8;

// Most enclosed course shells advertise themselves through hw4 bit 0x10. Bikini
// Island has one authoring exception: zone 13's final section (DL 0x803903B8)
// is a large rock wedge outside the tunnel. The original PVS hides it from zone
// 15, but merging it into the always-visible hw6=2 bucket makes it cut across
// the tunnel road.
constexpr bool pvs_gated_section(int track, uint8_t zone, uint32_t entry_index,
                                 uint32_t display_list, uint16_t hw4) {
    return (hw4 & 0x10u) != 0 ||
           (track == kBikiniIslandTrack && zone == kBikiniTunnelRockZone &&
            entry_index == kBikiniTunnelRockEntry &&
            display_list == kBikiniTunnelRockDisplayList);
}

} // namespace aero::full_track
