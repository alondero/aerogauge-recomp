#pragma once

#include <filesystem>

namespace aero::pak {
// Call before starting the guest. Only a missing image is formatted automatically.
void configure(const std::filesystem::path& path, bool enabled = true);
}
