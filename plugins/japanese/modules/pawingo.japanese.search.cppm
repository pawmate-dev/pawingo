module;
#include <ftxui/component/component.hpp>

export module pawingo.japanese.search;

import std;
export import pawingo.japanese.dictionary;
export import pawingo.japanese.examples;
export import pawingo.japanese.voicevox;
export import pawingo.plugin;

/// FTXUI presentation and host-service integration for Japanese vocabulary lookup.
export namespace pawingo::japanese {
/// Blocking speech adapter, invoked on a worker with cancellation; returns a UI status.
using Speech = std::function<std::string(const SpeechRequest &, std::stop_token)>;

/// Panel ownership and a weak-state accessor for the host's persistent footer.
struct SearchPanel {
  ftxui::Component component;
  std::function<std::string()> help;
};

/// Creates the vocabulary UI and binds frontend-neutral dictionary and host services.
///
/// @param dictionary Shared lookup cache retained until the panel's jobs finish.
/// @param host Borrowed storage/resource/redraw services; must outlive the panel.
/// @param speech Blocking pronunciation adapter receiving a word with its reading or a full
/// sentence.
/// @param examples Shared corpus owner; omitted for standalone panels, which create a local owner.
/// @return An independent panel and its current shortcut hint provider.
///
/// Panel destruction requests cancellation and joins its worker before releasing services.
/// Only UI callbacks access host persistence; workers request redraw through UiRefresh.
SearchPanel search_panel(std::shared_ptr<Dictionary> dictionary,
  Host &host,
  Speech speech,
  std::shared_ptr<Examples> examples = {});
} // namespace pawingo::japanese
