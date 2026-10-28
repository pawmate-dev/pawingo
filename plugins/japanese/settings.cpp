#include "settings.hpp"
#include "pawingo_voicevox_assets.hpp"

namespace pawingo::japanese {
const config::Settings &settings() {
  static const config::Settings value(assets::japanese_defaults, "japanese/defaults.yaml");
  return value;
}

const config::Settings &kana_settings() {
  static const config::Settings value(assets::japanese_kana, "japanese/kana.yaml");
  return value;
}

std::string ui_text(std::string_view key) {
  static const config::Settings copy(assets::japanese_messages, "japanese/messages.yaml");
  return copy.text("messages." + std::string(key));
}
} // namespace pawingo::japanese
