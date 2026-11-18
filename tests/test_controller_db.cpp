#undef NDEBUG
// User-supplied gamecontrollerdb.txt discovery and load-order contract.
// Compile from the repo root:
//   g++ -std=c++20 -o test_controller_db tests/test_controller_db.cpp -I src
#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

#include "aero_controller_db.h"

int main() {
    using aero::controller_db::candidate_paths;
    using aero::controller_db::kDatabaseFilename;

    assert(std::string(kDatabaseFilename) == "gamecontrollerdb.txt");

    // Empty inputs yield no paths.
    assert(candidate_paths({}, {}).empty());

    const auto tmp = std::filesystem::temp_directory_path() /
                     ("aero-controller-db-" + std::to_string(
                          std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto exe_dir = tmp / "exe";
    const auto data_dir = tmp / "data";
    std::filesystem::create_directories(exe_dir);
    std::filesystem::create_directories(data_dir);

    // Android-style (no exe_dir) returns only app_data_dir.
    {
        const auto paths = candidate_paths(data_dir, {});
        assert(paths.size() == 1);
        assert(paths[0] == data_dir / kDatabaseFilename);
    }

    // Desktop with separate exe_dir and app_data_dir loads exe_dir first so
    // per-user app_data_dir entries override exe-adjacent entries.
    {
        const auto paths = candidate_paths(data_dir, exe_dir);
        assert(paths.size() == 2);
        assert(paths[0] == exe_dir / kDatabaseFilename);
        assert(paths[1] == data_dir / kDatabaseFilename);
    }

    // Portable mode / identical directories (even with trailing separator)
    // collapse to one path so the same file is not loaded twice.
    {
        const auto exe_with_slash = exe_dir / "";
        const auto paths = candidate_paths(exe_dir, exe_with_slash);
        assert(paths.size() == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    return 0;
}
