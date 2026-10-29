module;
#include <ftxui/component/component.hpp>

export module pawingo.packages_ui;

import std;
export import pawingo.packages;
export import pawingo.storage;

export namespace pawingo::ui {
/// Owned catalog view and a weak-state provider for its current fixed-footer shortcuts.
struct PackagePanel {
  ftxui::Component component;
  std::function<std::string()> help;
};

/// Creates the offline-first catalog view. Store is borrowed on the UI thread for service policy;
/// downloads and installation run on one owned worker. Escape cancels prompts/jobs before leaving.
/// Manager and redraw are retained until cancellation and worker join complete. No network starts
/// merely by opening this panel. Installed changes are activated only on the next application run.
PackagePanel
package_panel(std::shared_ptr<packages::Manager>, Store &, std::function<void()> redraw = {});
} // namespace pawingo::ui
