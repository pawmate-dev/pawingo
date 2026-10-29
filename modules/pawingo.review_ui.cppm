module;
#include <ftxui/component/component.hpp>

export module pawingo.review_ui;

import std;
export import pawingo.storage;

export namespace pawingo::ui {
using AnswerMatcher = std::function<MatchResult(const Vocabulary &, const std::string &)>;
using ReviewClock = std::function<std::chrono::sys_seconds()>;

/// Shared review view and its current fixed-footer hints.
struct ReviewPanel {
  ftxui::Component component;
  std::function<std::string()> help;
};

/// Creates a meaning-review session over saved vocabulary, without network access.
/// Language must be a nonempty saved-vocabulary language code; empty values throw invalid_argument.
/// Every queue load and refresh stays within that language.
/// Store and any services borrowed by matcher must outlive the panel. Empty matcher uses the
/// conservative shared meaning matcher; empty clock uses the current Unix-second system time.
/// Enter checks an answer; a second confirmation or rating shortcut atomically commits it.
/// Escape from feedback edits the answer; Escape from a question delegates back navigation.
ReviewPanel review_panel(Store &,
  const std::string &language,
  AnswerMatcher matcher = {},
  ReviewClock clock = {});

/// Optional host capability for plugin-owned review panels, without exposing the database.
/// This separate interface leaves the native Host and LanguagePlugin vtables unchanged.
class ReviewProvider {
public:
  virtual ~ReviewProvider() = default;
  /// Creates a language-scoped session; the plugin supplies its answer matcher.
  /// The host and matcher services must outlive the returned panel. Creation failures propagate.
  virtual ReviewPanel create_review_panel(const std::string &language, AnswerMatcher matcher) = 0;
};
} // namespace pawingo::ui
