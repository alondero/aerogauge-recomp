# ADR 0003: Port owns settings storage; frontend owns edit state

- Status: records the current implementation; long-term approval pending
- Date: 2026-09-17

## Context

The shared frontend provides page state, option controls, and Apply or
Discard behavior. AeroGauge also has port-specific JSON keys, environment
overrides, window state, and enhancement files that the shared frontend does
not own.

The frontend callbacks may run while the renderer is holding the integration
lock. JSON writes and SDL window changes belong on the SDL main thread.

## Current implementation

Use RecompFrontend for settings pages and temporary edits, but keep
`graphics.json` and `enhancements.json` in `aero_config`. Mark the frontend
pages as externally stored. A callback captures the proposed value and queues
the permanent change for `aero_menu::update` on the SDL main thread.

This records the boundary that is in the current source. It does not approve
the lock or action-queue design as the long-term architecture.

Apply merges only fields edited on the page into the current port snapshot.
This prevents a fullscreen hotkey or an unknown JSON key from being silently
replaced by an older page snapshot.

## Trade-off

The port keeps two small adapters and must track the frontend configuration
API. The current action queue shares a lock with rendering. A blocking action
can delay rendering or deadlock if a future action waits for frontend work.
That risk is documented and is not solved by this decision.

## Revisit when

Revisit this boundary when the runtime or frontend provides a host-storage API
and a non-blocking action boundary. Compare an upstream contribution, a
smaller adapter, and a source-level settings service before moving ownership.
