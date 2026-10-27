#include "pawingo/plugin.hpp"
#include "pawingo/ui/theme.hpp"
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

class Host : public pawingo::Host {
public:
  void save(const pawingo::Vocabulary &) override {
  }
  std::vector<pawingo::Resource> resources(pawingo::ResourceKind, const std::string &) override {
    return {};
  }
};
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "plugin path required");
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib)
      throw std::runtime_error(dlerror());
    auto entry = reinterpret_cast<pawingo::PluginEntry>(dlsym(lib, pawingo::plugin_entry_symbol));
    require(entry != nullptr, "entry point");
    const auto *descriptor = entry();
    auto *plugin = descriptor->create();
    Host host;
    {
      auto panel = plugin->create_panel("五十音図", host);
      require(bool(panel), "panel");
      auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(40));
      ftxui::Render(screen, panel->Render());
      auto text = screen.ToString();
      require(text.find("あ") != std::string::npos && text.find("ア") != std::string::npos,
        "both scripts");
      require(!panel->OnEvent(ftxui::Event::Character('k')), "removed script key");
      auto *help = dynamic_cast<pawingo::PanelHelpProvider *>(plugin);
      require(help && help->panel_help("五十音図").find("h/k") == std::string::npos,
        "panel footer");
      require(panel->OnEvent(ftxui::Event::Character('d')), "voice state");
      require(panel->OnEvent(ftxui::Event::ArrowDown), "navigation");
    }
    char temporary[] = "/tmp/pawingo-japanese-test-XXXXXX";
    require(mkdtemp(temporary) != nullptr, "temporary resources");
    const std::filesystem::path directory(temporary);
    const std::string original_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    const char *data = std::getenv("XDG_DATA_HOME");
    const bool had_data = data != nullptr;
    const std::string original_data = data ? data : "";
    {
      std::ofstream script(directory / "espeak-ng");
      script << "#!/bin/sh\nsleep 3\n";
    }
    chmod((directory / "espeak-ng").c_str(), 0700);
    setenv("PATH", (directory.string() + ":" + original_path).c_str(), 1);
    setenv("XDG_DATA_HOME", directory.c_str(), 1);
    {
      auto panel = plugin->create_panel("五十音図", host);
      const auto started = std::chrono::steady_clock::now();
      require(panel->OnEvent(ftxui::Event::Return), "speech request");
      require(panel->OnEvent(ftxui::Event::ArrowDown), "navigation during speech");
      require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(300),
        "speech must not block input");
      auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(40));
      ftxui::Render(screen, panel->Render());
      bool highlighted = false;
      for (int y = 0; y < screen.dimy(); ++y)
        for (int x = 0; x < screen.dimx(); ++x) {
          const auto &pixel = screen.PixelAt(x, y);
          if (pixel.character == "い" && pixel.foreground_color == pawingo::ui::accent)
            highlighted = true;
          require(!pixel.inverted, "kana chart uses color without inverse blocks");
        }
      require(highlighted, "selection updates during speech");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto leaving = std::chrono::steady_clock::now();
      panel.reset();
      require(std::chrono::steady_clock::now() - leaving < std::chrono::milliseconds(500),
        "cancel playback when leaving");
    }
    const auto voice = directory / "pawingo/tts/japanese/mei_normal.htsvoice";
    std::filesystem::create_directories(voice.parent_path());
    {
      std::ofstream file(voice);
      file << "[GLOBAL]\nHTS_VOICE_VERSION:1.0\n";
    }
    std::filesystem::resize_file(voice, 862503);
    for (int reopen = 0; reopen < 2; ++reopen) {
      auto panel = plugin->create_panel("五十音図", host);
      panel->OnEvent(ftxui::Event::Character('d'));
      auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(40));
      ftxui::Render(screen, panel->Render());
      auto content = screen.ToString();
      require(content.find("already installed") != std::string::npos,
        "reuse installed voice after reopening");
      require(content.find("Confirm") == std::string::npos, "no redundant download confirmation");
    }
    setenv("PATH", original_path.c_str(), 1);
    if (had_data)
      setenv("XDG_DATA_HOME", original_data.c_str(), 1);
    else
      unsetenv("XDG_DATA_HOME");
    std::filesystem::remove_all(directory);
    descriptor->destroy(plugin);
    dlclose(lib);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
