module;
#include <ftxui/component/component.hpp>

export module pawingo.japanese.voice_panel;

import std;
export import pawingo.japanese.voicevox;
export import pawingo.plugin;

export namespace pawingo::japanese {
/// Creates the offline resource editor and model-derived speaker/style selector.
ftxui::Component voicevox_panel(std::shared_ptr<Voicevox>, pawingo::Host &);
} // namespace pawingo::japanese
