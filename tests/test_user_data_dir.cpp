#undef NDEBUG
// One per-user data folder. Compile from the repo root:
//   g++ -std=c++20 -o test_user_data_dir tests/test_user_data_dir.cpp -I src
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "aero_paths.h"

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    assert(input && "repo source must be readable via __FILE__");
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

} // namespace

// aero_config.cpp queues graphics changes through ultramodern. This TU does not
// link that; the path helper is the contract under test.
int main() {
    using aero::paths::kAppFolderName;
    using aero::paths::app_data_dir;
    using aero::paths::renderer_storage;

    // This is the conversion used by RT64's startup and live texture updates.
    // Never substitute an empty native path for a failed conversion: the
    // replacement loader could then interpret it as the working directory.
    assert(!aero::paths::from_utf8(""));
    const auto ascii = aero::paths::from_utf8("packs with spaces/test.rtz");
    assert(ascii && *ascii == std::filesystem::path("packs with spaces/test.rtz"));
    const std::u8string unicode = u8"packs/\u65e5\u672c\U0001f3ce/pack.rtz";
    const std::string unicode_bytes(reinterpret_cast<const char*>(unicode.data()), unicode.size());
    const auto decoded = aero::paths::from_utf8(unicode_bytes);
    assert(decoded && *decoded == std::filesystem::path(unicode));
    for (const auto& malformed : {std::string("\xff\xfe"), std::string("\xe2\x82"),
                                 std::string("\x80"), std::string("\xc0\xaf"),
                                 std::string("\xed\xa0\x80")}) {
        const auto path = aero::paths::from_utf8(malformed);
#if defined(_WIN32)
        assert(!path && "unconvertible input must be skipped without throwing");
#else
        // POSIX paths can contain arbitrary bytes; no transcoding is required.
        assert(path && path->native() == malformed);
#endif
    }
    assert(aero::paths::from_utf8(unicode_bytes) == decoded);

    assert(std::string(kAppFolderName) == "AeroGaugeRecomp");

    const auto storage = renderer_storage();
    assert(std::string(storage.app_id) == kAppFolderName);
    assert(!storage.detect_data_path);
    assert(storage.data_path == app_data_dir());
    assert(storage.data_path.filename() != "aerogauge-recomp");

    const auto repo = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::string renderer = read_file(repo / "src" / "rt64_renderer.cpp");
    assert(renderer.find("\"aerogauge-recomp\"") == std::string::npos);
    assert(renderer.find("renderer_storage") != std::string::npos);

    const auto tmp = std::filesystem::temp_directory_path() /
                     ("aero-user-data-dir-" + std::to_string(
                          std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp);
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(tmp);
    {
        std::error_code ec;
        assert(!std::filesystem::exists("portable.txt", ec));
        const auto dir = app_data_dir();
        assert(dir.filename() == kAppFolderName);
        assert(dir.filename() != "aerogauge-recomp");
    }
    {
        std::ofstream marker(tmp / "portable.txt");
        marker << '\n';
    }
    assert(app_data_dir() == tmp);
    std::filesystem::current_path(previous);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    return 0;
}
