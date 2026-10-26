// Workflow expectations: explicit CLI inputs are authoritative; first-launch
// desktop setup may retry/cancel, automated runs must never open a dialog.
// Runtime hash and byte-order validation are checked separately with real ROMs.
#undef NDEBUG
#include "aero_rom_startup.h"
#include <cassert>
#include <vector>

int main() {
    using namespace aero::rom;
    using Path = std::filesystem::path;
    const std::vector<Path> candidates{"local-us", "local-jp", "cache-us", "cache-jp"};
    std::vector<Path> seen;
    int prompts = 0;
    auto validate = [&](const Path& path) -> Validation {
        seen.push_back(path);
        if (path == "local-jp" || path == "picked-jp") return {Region::JapanRevA, {}};
        if (path == "cache-us") return {Region::USA, {}};
        return {{}, "unsupported file"};
    };
    auto never_pick = [&](const std::string&) -> std::optional<Path> {
        ++prompts;
        assert(false && "explicit paths must not prompt");
        return {};
    };
    auto result = resolve(Path("bad-explicit"), candidates, validate, never_pick);
    assert(!result.region && !result.cancelled && result.error == "unsupported file");
    assert(seen == std::vector<Path>{"bad-explicit"} && prompts == 0);
    seen.clear();
    result = resolve(Path("picked-jp"), candidates, validate, never_pick);
    assert(result.region == Region::JapanRevA && seen == std::vector<Path>{"picked-jp"});

    seen.clear();
    result = resolve({}, candidates, validate);
    assert(result.region == Region::JapanRevA);
    assert((seen == std::vector<Path>{"local-us", "local-jp"}));
    // A valid local Japanese ROM precedes a cached USA ROM; USA still wins if
    // both local files are valid because the caller orders USA first.
    seen.clear();
    const std::vector<Path> cache_only{"corrupt-cache", "cache-us", "cache-jp"};
    result = resolve({}, cache_only, validate);
    assert(result.region == Region::USA);
    assert((seen == std::vector<Path>{"corrupt-cache", "cache-us"}));

    const std::vector<Path> invalid{"corrupt-cache"};
    seen.clear();
    result = resolve({}, invalid, validate); // no picker: headless/Android
    assert(!result.region && !result.cancelled && result.error == "unsupported file");
    assert(seen == invalid);
    result = resolve({}, {}, validate);
    assert(!result.region && !result.cancelled && !result.error.empty());

    prompts = 0;
    seen.clear();
    result = resolve({}, {}, validate, [&](const std::string& error) -> std::optional<Path> {
        ++prompts;
        if (prompts == 1) { assert(!error.empty()); return "bad-pick"; }
        assert(error == "unsupported file");
        return "picked-jp";
    });
    assert(result.region == Region::JapanRevA && prompts == 2);
    assert((seen == std::vector<Path>{"bad-pick", "picked-jp"}));

    result = resolve({}, invalid, validate, [](const std::string&) -> std::optional<Path> { return {}; });
    assert(!result.region && result.cancelled && result.error.empty());
}
