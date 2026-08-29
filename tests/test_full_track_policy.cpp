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
}
