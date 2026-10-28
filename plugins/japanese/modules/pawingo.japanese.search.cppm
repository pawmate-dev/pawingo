module;
#include <ftxui/component/component.hpp>

export module pawingo.japanese.search;

import std;
export import pawingo.japanese.dictionary;
export import pawingo.plugin;

/// FTXUI presentation and host-service integration for Japanese vocabulary lookup.
export namespace pawingo::japanese {
/// Blocking speech adapter, invoked on a worker with cancellation; returns a UI status.
using Speech = std::function<std::string(const Vocabulary &, std::stop_token)>;

/// Panel ownership and a weak-state accessor for the host's persistent footer.
struct SearchPanel {
  ftxui::Component component;
  std::function<std::string()> help;
};

/// Creates the vocabulary UI and binds frontend-neutral dictionary and host services.
///
/// @param dictionary Shared lookup cache retained until the panel's jobs finish.
/// @param host Borrowed storage/resource/redraw services; must outlive the panel.
/// @param speech Blocking pronunciation adapter retaining the selected spelling and reading.
/// @return An independent panel and its current shortcut hint provider.
///
/// Panel destruction requests cancellation and joins its worker before releasing services.
/// Only UI callbacks access host persistence; workers request redraw through UiRefresh.
SearchPanel search_panel(std::shared_ptr<Dictionary> dictionary, Host &host, Speech speech);
} // namespace pawingo::japanese
