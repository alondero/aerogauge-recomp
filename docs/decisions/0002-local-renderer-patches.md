# ADR 0002: Keep renderer patches explicit while their boundary is tested

- Status: accepted transition policy
- Date: 2026-09-14

## Context

The port uses RT64 for normal display-list interpretation and presentation,
but the pinned stack carries local fixes for interpolation, sky backdrops,
wide split-screen viewports, matrix decomposition, overscan, and MinGW/D3D12
compatibility. Some may be general RT64 behavior. Others are AeroGauge
camera or display-list policy.

## Decision

Keep each local dependency change as a numbered patch against a pinned
submodule commit until its boundary is tested. A patch is a candidate for
upstream only when a minimal reproduction shows that the behavior is general.
ROM-specific HUD classification, course registration, and projection policy
remain in the port.

Before removing or upstreaming a patch, record the pinned source comparison,
the smallest failing case, a regression test, and the maintainer's decision.

## Consequences

The build is more work to refresh, and local patches can drift. In return,
the source makes dependency changes reviewable and does not hide game-specific
assumptions inside a general renderer. The [renderer investigation](../investigations/2026-09-14-renderer-audit.md)
tracks the remaining questions.
