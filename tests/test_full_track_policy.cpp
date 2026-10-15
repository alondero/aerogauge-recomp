#undef NDEBUG
#include <cassert>

#include "src/aero_full_track_policy.h"

int main() {
    using aero::full_track::pvs_gated_section;

    // The ROM-authored shell flag remains the general rule on every course.
    assert(pvs_gated_section(0u, 4u, 0u, 0x10u));
    assert(pvs_gated_section(1u, 21u, 0u, 0x18u));

    // Bikini zone 13 entry 11 is the unflagged rock wedge outside the tunnel.
    assert(pvs_gated_section(1u, 13u, 0x803903B8u, 0x00u));
    assert(!pvs_gated_section(1u, 13u, 0x803903B0u, 0x00u));

    // Keep the exception narrow: adjacent zones and tracks still merge.
    assert(!pvs_gated_section(1u, 12u, 0x803903B8u, 0x00u));
    assert(!pvs_gated_section(0u, 13u, 0x803903B8u, 0x00u));

    // Bikini zone 23 entry 0 caps the rotating tunnel from zone 20's view.
    assert(pvs_gated_section(1u, 23u, 0x80396070u, 0x00u));
    assert(!pvs_gated_section(1u, 23u, 0x803961C8u, 0x00u));
    assert(!pvs_gated_section(1u, 22u, 0x80396070u, 0x00u));
    assert(!pvs_gated_section(0u, 23u, 0x80396070u, 0x00u));

    // Chinatown's early-road wall tops overlap the later elevated road.
    assert(pvs_gated_section(2u, 4u, 0x803C8220u, 0x00u));
    assert(!pvs_gated_section(2u, 4u, 0x803C8228u, 0x00u));
    assert(!pvs_gated_section(2u, 3u, 0x803C8220u, 0x00u));
    assert(!pvs_gated_section(3u, 4u, 0x803C8220u, 0x00u));
    // Japan Rev A's corresponding Chinatown mesh is relocated by 0x90.
    assert(pvs_gated_section(2u, 4u, 0x803C82B0u, 0x00u));
    assert(!pvs_gated_section(2u, 4u, 0x803C82B8u, 0x00u));
    assert(!pvs_gated_section(2u, 3u, 0x803C82B0u, 0x00u));
    assert(!pvs_gated_section(3u, 4u, 0x803C82B0u, 0x00u));

    // Chinatown Jam uses a different mesh and zone for the overlapping tops.
    assert(pvs_gated_section(4u, 9u, 0x803CD540u, 0x00u));
    assert(!pvs_gated_section(4u, 9u, 0x803CD548u, 0x00u));
    assert(!pvs_gated_section(4u, 8u, 0x803CD540u, 0x00u));
    assert(!pvs_gated_section(5u, 9u, 0x803CD540u, 0x00u));
    assert(pvs_gated_section(4u, 9u, 0x803CD5D0u, 0x00u));
    assert(!pvs_gated_section(4u, 9u, 0x803CD5D8u, 0x00u));
    assert(!pvs_gated_section(4u, 8u, 0x803CD5D0u, 0x00u));
    assert(!pvs_gated_section(5u, 9u, 0x803CD5D0u, 0x00u));
}
