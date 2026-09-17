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
