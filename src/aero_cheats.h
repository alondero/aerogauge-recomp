#pragma once

#include <array>
#include <cstddef>

namespace aero::cheats {
enum class Cheat { InfiniteDamage, AlwaysFirstPosition, Count };
struct Description {
    Cheat cheat;
    const char* id;
    const char* name;
    const char* description;
};
inline constexpr std::array<Description, static_cast<std::size_t>(Cheat::Count)> catalog{{
    {Cheat::InfiniteDamage, "infinite_damage", "Infinite Damage (Player 1)",
     "Keep Player 1's damage at zero from the next vehicle update. Does not revive an already destroyed car. Disable to take damage normally. Resets when the application closes."},
    {Cheat::AlwaysFirstPosition, "always_first_position", "Always First Position (Player 1)",
     "Place Player 1 first at each standings update, keeping every opponent in the race. You must still finish the race. Results and saved progress can retain cheated wins. Resets when the application closes."},
}};

// UI threads publish independent flags; game-thread hooks consume them.
// Session-only and initially off. Neither config files nor guest saves own them.
bool enabled(Cheat cheat);
void set_enabled(Cheat cheat, bool value);
}
