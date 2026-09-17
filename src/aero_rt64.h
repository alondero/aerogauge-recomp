#ifndef AERO_RT64_H
#define AERO_RT64_H

// RT64 is the normal presenter for the runtime. It is a build dependency, but the
// process can run without a window when AERO_HEADLESS=1. That mode uses the software
// renderer and is intended for deterministic framebuffer tests and diagnostics.
// If RT64 cannot create a window or graphics device, the caller uses the same fallback.

#include <memory>

#include "ultramodern/renderer_context.hpp"

namespace aero_rt64 {

// True unless the harness asked for headless (AERO_HEADLESS=1). When true, the port
// creates an SDL window and presents through RT64.
bool enabled();

// Instantiates the RT64-backed RendererContext. Returns nullptr when RT64 setup fails
// (no Vulkan device, no window, ...) so the caller can fall back to headless swrender.
std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle,
                      bool developer_mode);

} // namespace aero_rt64

#endif // AERO_RT64_H
