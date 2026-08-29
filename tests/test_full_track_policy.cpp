#include "src/aero_full_track_policy.h"

int main() {
    using aero::full_track::pvs_gated_section;
    using aero::full_track::kBikiniIslandTrack;
    using aero::full_track::kBikiniTunnelRockZone;
    using aero::full_track::kBikiniTunnelRockEntry;
    using aero::full_track::kBikiniTunnelRockDisplayList;

    // The ROM-authored shell flag remains the general rule on every course.
    if (!pvs_gated_section(0, 4, 2, 0, 0x10)) return 1;
    if (!pvs_gated_section(1, 21, 0, 0, 0x18)) return 2;

    // Bikini zone 13 entry 11 is the unflagged rock wedge outside the tunnel.
    if (!pvs_gated_section(kBikiniIslandTrack, kBikiniTunnelRockZone,
                           kBikiniTunnelRockEntry,
                           kBikiniTunnelRockDisplayList, 0x00)) return 3;
    if (pvs_gated_section(kBikiniIslandTrack, kBikiniTunnelRockZone,
                          kBikiniTunnelRockEntry, 0x803903B0, 0x00)) return 4;

    // Keep the exception narrow: adjacent entries, zones, and tracks still merge.
    if (pvs_gated_section(kBikiniIslandTrack, kBikiniTunnelRockZone,
                          kBikiniTunnelRockEntry - 1,
                          kBikiniTunnelRockDisplayList, 0x00)) return 5;
    if (pvs_gated_section(kBikiniIslandTrack, kBikiniTunnelRockZone - 1,
                          kBikiniTunnelRockEntry, kBikiniTunnelRockDisplayList,
                          0x00)) return 6;
    if (pvs_gated_section(kBikiniIslandTrack - 1, kBikiniTunnelRockZone,
                          kBikiniTunnelRockEntry, kBikiniTunnelRockDisplayList,
                          0x00)) return 7;
    return 0;
}
