// Exercise the frontend's real RmlUi key-event and navigation-tree boundary.
// No ROM, SDL window, or graphics device is needed.
#include "recompui/recompui.h"
#include "elements/ui_document.h"
#include "librecomp/game.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;

namespace {
class NullRenderer : public Rml::RenderInterface {
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        NullRenderer renderer;
        Rml::SetRenderInterface(&renderer);
        require(Rml::Initialise(), "RmlUi initialization failed");
        auto* rml = Rml::CreateContext("navigation-test", {800, 600});
        require(rml != nullptr, "RmlUi context creation failed");
        auto* document = rml->CreateDocument();
        auto context = recompui::create_context(document);
        context.open();
        auto* root = context.get_root_element();
        auto* outer = context.create_element<recompui::Element>(root);
        outer->set_as_navigation_container(recompui::NavigationType::Vertical);
        outer->set_nav_wrapping(true);
        std::vector<recompui::Element*> items;
        for (int row = 0; row < 3; ++row) {
            auto* container = context.create_element<recompui::Element>(outer);
            container->set_as_navigation_container(recompui::NavigationType::Horizontal);
            container->set_nav_wrapping(true);
            for (int col = 0; col < 3; ++col) {
                auto* item = context.create_element<recompui::Element>(container);
                item->enable_focus();
                item->set_width(40);
                item->set_height(20);
                item->set_as_primary_focus(col == 0);
                items.push_back(item);
            }
        }
        document->Show();
        rml->Update();
        require(items[0]->focus(), "Initial focus failed");
        context.close();
        const int expected_indices[] = {6, 8, 0, 1};
        int direction = 0;
        for (auto key : {Rml::Input::KI_DOWN, Rml::Input::KI_RIGHT, Rml::Input::KI_UP, Rml::Input::KI_LEFT}) {
            for (int i = 0; i < 20; ++i) {
                rml->ProcessKeyDown(key, 0);
                rml->ProcessKeyUp(key, 0);
                rml->Update();
            }
            context.open();
            require(context.get_focused_element() == items[expected_indices[direction++]],
                    "Directional navigation must reach the expected row and column");
            context.close();
        }
        context.open();
        require(context.get_focused_element() != nullptr, "Navigation lost focus");
        outer->clear_children();
        auto* group = context.create_element<recompui::Element>(outer);
        group->enable_focus();
        group->set_as_navigation_container(recompui::NavigationType::Horizontal);
        // Keep context mutation separate from the real key-event path.
        auto navigate = [&](Rml::Input::KeyIdentifier key) {
            context.close();
            rml->ProcessKeyDown(key, 0);
            rml->ProcessKeyUp(key, 0);
            rml->Update();
            context.open();
        };
        auto make_control = [&](recompui::Element* parent) {
            auto* control = context.create_element<recompui::Element>(parent);
            control->enable_focus();
            control->set_width(40);
            control->set_height(20);
            return control;
        };
        const std::string_view mode = argc > 1 ? argv[1] : "wrap";
        if (mode == "multi") {
            group->set_position(recompui::Position::Relative);
            group->set_width(300);
            group->set_height(300);
            std::vector<recompui::Element*> candidates;
            for (int offset : {0, 100, 120}) {
                auto* control = make_control(group);
                control->set_position(recompui::Position::Absolute);
                control->set_left(offset);
                control->set_top(offset);
                candidates.push_back(control);
            }
            rml->Update();
            // The middle control is included in the wrap destination, but the
            // nearest other candidate must still win in either direction.
            for (auto key : {Rml::Input::KI_DOWN, Rml::Input::KI_UP}) {
                require(candidates[1]->focus(), "Multi-candidate focus failed");
                navigate(key);
                require(context.get_focused_element() == candidates[2],
                        "Wrapping must select the nearest other candidate");
            }
        } else if (mode == "entry") {
            auto* child = make_control(group);
            child->set_as_primary_focus(true);
            rml->Update();
            require(group->focus(), "Container focus failed");
            navigate(Rml::Input::KI_DOWN);
            require(context.get_focused_element() == child,
                    "A focused container must enter its live children without a cached tree");
            require(group->focus(), "Container refocus failed");
            navigate(Rml::Input::KI_UP);
            require(context.get_focused_element() == child,
                    "A focused container must enter its live children after rebuilding");
        } else if (mode == "stale") {
            auto* child = make_control(group);
            child->set_as_primary_focus(true);
            rml->Update();
            require(child->focus(), "Child focus failed");
            navigate(Rml::Input::KI_DOWN);
            require(group->get_nav_children()->size() == 1,
                    "The navigation pass must cache the live child");
            require(group->focus(), "Container focus failed");
            group->display_hide();
            group->clear_children();
            // This public cache accessor gives a deterministic lifetime check
            // before any rebuild or allocator reuse can mask a freed pointer.
            require(group->get_nav_children()->empty(),
                    "clear_children must invalidate cached navigation descendants immediately");
            group->display_show();
            navigate(Rml::Input::KI_DOWN);
            require(context.get_focused_element() == group,
                    "An empty focused container must retain focus after deleting its children");
            require(std::find(outer->get_nav_children()->begin(), outer->get_nav_children()->end(), group)
                        != outer->get_nav_children()->end(),
                    "A focusable empty container must remain in its parent's navigation tree");
            auto* replacement = make_control(group);
            replacement->set_as_primary_focus(true);
            rml->Update();
            navigate(Rml::Input::KI_DOWN);
            require(context.get_focused_element() == replacement,
                    "Navigation must enter replacement children of a focused container");
        } else if (mode == "grid") {
            outer->set_as_navigation_container(recompui::NavigationType::GridCol);
            outer->set_nav_wrapping(false);
            group->set_as_navigation_container(recompui::NavigationType::GridRow);
            auto* child = make_control(group);
            auto* empty_row = context.create_element<recompui::Element>(outer);
            empty_row->enable_focus();
            empty_row->set_as_navigation_container(recompui::NavigationType::GridRow);
            rml->Update();
            require(child->focus(), "Grid child focus failed");
            navigate(Rml::Input::KI_DOWN);
            require(context.get_focused_element() == empty_row,
                    "Grid navigation must focus an empty row without indexing its children");
            navigate(Rml::Input::KI_UP);
            require(context.get_focused_element() == child,
                    "Grid navigation must return from an empty row to the live control");
        } else if (mode == "remove") {
            auto* wrapper = context.create_element<recompui::Element>(group);
            auto* child = make_control(wrapper);
            auto* survivor = make_control(group);
            survivor->set_as_primary_focus(true);
            rml->Update();
            require(child->focus(), "Removed child focus failed");
            navigate(Rml::Input::KI_DOWN);
            require(group->get_nav_children()->size() == 2,
                    "The parent navigation cache must include flattened descendants");
            require(wrapper->remove_child(child), "Removing nested child failed");
            require(group->get_nav_children()->empty(),
                    "Removing a flattened descendant must invalidate the ancestor navigation cache");
            navigate(Rml::Input::KI_DOWN);
            require(context.get_focused_element() == survivor,
                    "Navigation must retain the surviving control after removal");
            require(group->focus(), "Container focus failed");
            require(group->remove_child(survivor), "Removing direct child failed");
            require(group->get_nav_children()->empty(),
                    "Removing a direct child must invalidate the navigation cache");
            navigate(Rml::Input::KI_DOWN);
            require(context.get_focused_element() == group,
                    "Navigation must retain an empty container after removing its last control");
        } else {
            require(mode == "wrap", "Unknown navigation test mode");
            auto* child = context.create_element<recompui::Element>(group);
            child->set_as_primary_focus(true);
            child->set_as_navigation_container(recompui::NavigationType::Auto);
            auto* focused = make_control(child);
            rml->Update();
            require(focused->focus(), "Child focus failed");
            for (auto key : {Rml::Input::KI_DOWN, Rml::Input::KI_UP}) {
                navigate(key);
                require(context.get_focused_element() == focused,
                        "Wrapping a single-item group must retain its focused descendant");
            }
        }
        context.close();
        recompui::destroy_context(context);
        Rml::Shutdown();
        std::puts("PASS: frontend navigation");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
