export module pawingo.japanese.settings;

import std;
export import pawingo.config;

export namespace pawingo::japanese {
/// Embedded language defaults and learning content, independent of FTXUI.
const config::Settings &settings();
const config::Settings &kana_settings();
/// Returns language-plugin presentation copy from its embedded catalog.
std::string ui_text(std::string_view key);
} // namespace pawingo::japanese
