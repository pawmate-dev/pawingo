module;
#include <ftxui/component/component.hpp>

export module pawingo.review_ui;

import std;
export import pawingo.storage;

export namespace pawingo::ui {
using AnswerMatcher = std::function<MatchResult(const Vocabulary &, const std::string &)>;
using ReviewClock = std::function<std::chrono::sys_seconds()>;

/// Host-owned review view and its current fixed-footer hints.
struct ReviewPanel {
  ftxui::Component component;
  std::function<std::string()> help;
};

/// Creates a meaning-review session over saved vocabulary, without network access.
/// Store and any services borrowed by matcher must outlive the panel. Empty matcher uses the
/// conservative shared meaning matcher; empty clock uses the current Unix-second system time.
/// Enter checks an answer; a second confirmation or rating shortcut atomically commits it.
/// Escape from feedback edits the answer; Escape from a question delegates back navigation.
ReviewPanel review_panel(Store &, AnswerMatcher matcher = {}, ReviewClock clock = {});
} // namespace pawingo::ui
