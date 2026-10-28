#pragma once
#include "pawingo/config/settings.hpp"

namespace pawingo::japanese {
/// Embedded language defaults and learning content, independent of FTXUI.
const config::Settings &settings();
const config::Settings &kana_settings();
/// Returns language-plugin presentation copy from its embedded catalog.
std::string ui_text(std::string_view key);
} // namespace pawingo::japanese
