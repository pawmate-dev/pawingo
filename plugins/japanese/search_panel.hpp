#pragma once
#include "dictionary.hpp"
#include "pawingo/plugin.hpp"
#include <functional>

/// FTXUI presentation and host-service integration for Japanese vocabulary lookup.
namespace pawingo::japanese {
/// Blocking speech adapter, invoked on a worker with cancellation; returns a UI status.
using Speech = std::function<std::string(const std::string &, std::stop_token)>;

/// Panel ownership and a weak-state accessor for the host's persistent footer.
struct SearchPanel {
  ftxui::Component component;
  std::function<std::string()> help;
};

/// Creates the vocabulary UI and binds frontend-neutral dictionary and host services.
///
/// @param dictionary Shared lookup cache retained until the panel's jobs finish.
/// @param host Borrowed storage/resource/redraw services; must outlive the panel.
/// @param speech Blocking pronunciation adapter accepting text and cancellation.
/// @return An independent panel and its current shortcut hint provider.
///
/// Panel destruction requests cancellation and joins its worker before releasing services.
/// Only UI callbacks access host persistence; workers request redraw through UiRefresh.
SearchPanel search_panel(std::shared_ptr<Dictionary> dictionary, Host &host, Speech speech);
} // namespace pawingo::japanese
