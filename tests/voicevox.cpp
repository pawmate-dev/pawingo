#include "../plugins/japanese/voicevox.hpp"
#include "pawingo/config/settings.hpp"
#include "pawingo/io/temporary_artifact.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace pawingo::japanese;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main() {
  try {
    pawingo::io::TemporaryArtifact directory(std::filesystem::temp_directory_path());
    auto root = directory.file().parent_path();
#ifdef _WIN32
    _putenv_s("XDG_CONFIG_HOME", root.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", root.c_str(), 1);
#endif
    require(Voicevox::load_config()->core.empty(), "fresh configuration");
    const auto legacy_path = pawingo::config::preferences_path("voicevox.json");
    std::filesystem::create_directories(legacy_path.parent_path());
    {
      std::ofstream legacy(legacy_path);
      legacy << R"({"core":"old-core","style":13})";
    }
    auto legacy = Voicevox::load_config();
    require(legacy && legacy->core == "old-core" && legacy->style == 13,
      "read legacy JSON without losing user preferences");
    VoicevoxConfig config{"/missing/core", "/runtime", "/dictionary", "/model", 42};
    require(bool(Voicevox::save_config(config)), "save configuration");
    auto restored = Voicevox::load_config();
    require(restored && restored->style == 42 && restored->model == config.model,
      "persist paths and stable style ID");
    config.style = 7;
    require(bool(Voicevox::save_config(config)) && Voicevox::load_config()->style == 7,
      "replace configuration");
    require(std::filesystem::exists(legacy_path), "saving YAML preserves legacy configuration");
    require(Voicevox::config_path().extension() == ".yaml", "new preferences use YAML");
    Voicevox service;
    auto styles = service.styles(config);
    require(!styles && styles.error().kind == VoicevoxErrorKind::runtime,
      "missing library returns recoverable error");
    std::stop_source stop;
    stop.request_stop();
    auto result = service.speak(config, "こんにちは", stop.get_token());
    require(!result && result.error().kind == VoicevoxErrorKind::cancelled,
      "cancel before native initialization");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
