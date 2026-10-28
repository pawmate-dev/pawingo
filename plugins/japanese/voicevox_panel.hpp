#pragma once
#include "pawingo/plugin.hpp"
#include "voicevox.hpp"

namespace pawingo::japanese {
/// Creates the offline resource editor and model-derived speaker/style selector.
ftxui::Component voicevox_panel(std::shared_ptr<Voicevox>, pawingo::Host &);
} // namespace pawingo::japanese
