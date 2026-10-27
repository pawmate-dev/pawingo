#include "pawingo/core.hpp"
#include <cstdlib>
#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::filesystem::path data_dir() {
  if (const char *p = std::getenv("XDG_DATA_HOME"); p && *p)
    return std::filesystem::path(p) / "pawingo";
  if (const char *p = std::getenv("HOME"); p && *p)
    return std::filesystem::path(p) / ".local/share/pawingo";
  throw std::runtime_error("HOME and XDG_DATA_HOME are unset");
}
} // namespace
int main() {
  try {
    auto directory = data_dir();
    std::filesystem::create_directories(directory);
    // Declaration order keeps the host and plugin libraries alive until their
    // FTXUI components have been destroyed after the event loop.
    pawingo::Store store((directory / "pawingo.sqlite3").string());
    pawingo::Plugins plugins;
    plugins.discover((directory / "plugins").string());
    using namespace ftxui;
    std::vector<std::string> names{"Home", "Saved vocabulary"};
    int selected = 0;
    auto pages = Container::Tab({}, &selected);
    auto home = Renderer([&] {
      Elements items{text("Pawingo") | bold,
        text("Install language plugins in: " + (directory / "plugins").string())};
      for (const auto &error : plugins.errors())
        items.push_back(text(error) | color(Color::Red));
      if (plugins.languages().empty())
        items.push_back(text("No language plugins installed."));
      return vbox(std::move(items));
    });
    pages->Add(home);
    pages->Add(Renderer([&] {
      Elements items{text("Saved vocabulary") | bold};
      for (const auto &v : store.vocabulary())
        items.push_back(text(v.expression + " — " + v.meaning + " [" + v.source + "]"));
      return vbox(std::move(items)) | vscroll_indicator | frame;
    }));
    for (auto *plugin : plugins.languages()) {
      for (const auto &id : plugin->panels()) {
        auto panel = plugin->create_panel(id, store);
        if (panel) {
          names.push_back(plugin->language() + ": " + id);
          pages->Add(panel);
        }
      }
    }
    auto menu = Menu(&names, &selected);
    auto screen = ScreenInteractive::TerminalOutput();
    auto quit = Button("Quit", screen.ExitLoopClosure());
    auto layout = Container::Vertical({menu, pages, quit});
    auto app = Renderer(layout, [&] {
      return vbox({text("Pawingo") | bold | center,
               separator(),
               menu->Render(),
               separator(),
               pages->Render() | flex,
               separator(),
               quit->Render(),
               text("Tab: switch control  •  Enter: select") | dim}) |
        border;
    });
    screen.Loop(app);
  } catch (const std::exception &e) {
    std::cerr << "Pawingo: " << e.what() << '\n';
    return 1;
  }
}
