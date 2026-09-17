#pragma once
#include <functional>

namespace aero::menu {
void create_settings();
void refresh_settings();
void enqueue(std::function<void()> action);
}
