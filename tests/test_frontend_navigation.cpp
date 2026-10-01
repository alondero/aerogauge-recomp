// Exercise the frontend's real RmlUi key-event and navigation-tree boundary.
// No ROM, SDL window, or graphics device is needed.
#include "recompui/recompui.h"
#include "elements/ui_document.h"
#include "librecomp/game.hpp"
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
        // A focused navigation container keeps its place while its controls
        // are replaced, as happens when a settings page changes content.
        outer->clear_children();
        auto* group = context.create_element<recompui::Element>(outer);
        group->enable_focus();
        group->set_as_navigation_container(recompui::NavigationType::Horizontal);
        auto* child = context.create_element<recompui::Element>(group);
        child->set_as_primary_focus(true);
        auto* focused = child;
        const bool stale = argc > 1 && std::string_view(argv[1]) == "stale";
        if (!stale) {
            child->set_as_navigation_container(recompui::NavigationType::Auto);
            focused = context.create_element<recompui::Element>(child);
        }
        focused->enable_focus();
        rml->Update();
        require(focused->focus(), "Child focus failed");
        context.close();
        rml->ProcessKeyDown(Rml::Input::KI_DOWN, 0);
        rml->ProcessKeyUp(Rml::Input::KI_DOWN, 0);
        context.open();
        require(context.get_focused_element() == focused,
                "Wrapping a single-item group must retain its focused descendant");
        // Keep the child alive for this assertion: the old cached-tree walk
        // deterministically refocuses it, independent of freed-memory contents.
        require(group->focus(), "Container focus failed");
        context.close();
        rml->ProcessKeyDown(Rml::Input::KI_DOWN, 0);
        rml->ProcessKeyUp(Rml::Input::KI_DOWN, 0);
        context.open();
        require(context.get_focused_element() == group,
                "A focused navigation container must be treated as a leaf");
        // Rebuild the descendant cache before exercising its destruction.
        require(focused->focus(), "Child refocus failed");
        context.close();
        rml->ProcessKeyDown(Rml::Input::KI_DOWN, 0);
        rml->ProcessKeyUp(Rml::Input::KI_DOWN, 0);
        context.open();
        group->clear_children();
        require(group->focus(), "Container focus failed");
        context.close();
        rml->ProcessKeyDown(Rml::Input::KI_DOWN, 0);
        rml->ProcessKeyUp(Rml::Input::KI_DOWN, 0);
        context.open();
        require(context.get_focused_element() == group,
                "Wrapping a focused container must retain focus after replacing its children");
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
