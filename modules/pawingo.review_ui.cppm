module;
#include <ftxui/component/component.hpp>

export module pawingo.review_ui;

import std;
export import pawingo.storage;

export namespace pawingo::ui {
using AnswerMatcher = std::function<MatchResult(const Vocabulary &, const std::string &)>;
using ReviewClock = std::function<std::chrono::sys_seconds()>;

/// Plugin-authored prompt; expected text is only rendered after checking the answer.
struct ReviewQuestion {
  std::string title, subtitle, expected, placeholder;
  bool audio_only = false;
};

/// Language-specific content; callbacks run on the UI owner except speak/redraw.
/// Enrollment only adds cards. Speech runs on a cancellable worker and must never access Store.
/// Captured services must outlive the panel; destruction cancels and joins speech before release.
struct ReviewContent {
  std::function<std::vector<review::QuestionType>(const Vocabulary &)> types;
  std::function<ReviewQuestion(const review::Card &)> question;
  std::function<MatchResult(const review::Card &, const std::string &)> match;
  std::function<std::expected<void, review::Error>(const Vocabulary &, std::stop_token)> speak;
  std::function<void()> redraw;
};

/// Shared review view and its current fixed-footer hints.
struct ReviewPanel {
  ftxui::Component component;
  std::function<std::string()> help;
};

/// Creates a typed-answer session over saved vocabulary; empty content uses meaning questions.
/// Plugin content can enroll additional question types without resetting existing cards.
/// Speech is explicit (Ctrl+P), asynchronous and joined before captured services are released.
/// Listening assessment requires completed playback; reading speech is hidden until feedback.
/// Language must be a nonempty saved-vocabulary language code; empty values throw invalid_argument.
/// Every queue load and refresh stays within that language.
/// Store and any services borrowed by matcher must outlive the panel. Empty matcher uses the
/// conservative shared meaning matcher; empty clock uses the current Unix-second system time.
/// Enter checks an answer; a second confirmation or rating shortcut atomically commits it.
/// Escape from feedback edits the answer; Escape from a question delegates back navigation.
ReviewPanel review_panel(Store &,
  const std::string &language,
  AnswerMatcher matcher = {},
  ReviewClock clock = {},
  ReviewContent content = {});

/// Optional host capability for plugin-owned review panels, without exposing the database.
/// This separate interface leaves the native Host and LanguagePlugin vtables unchanged.
class ReviewProvider {
public:
  virtual ~ReviewProvider() = default;
  /// Creates a language-scoped session; the plugin supplies its answer matcher.
  /// The host and matcher services must outlive the returned panel. Creation failures propagate.
  virtual ReviewPanel create_review_panel(const std::string &language,
    AnswerMatcher matcher,
    ReviewContent content = {}) = 0;
};
} // namespace pawingo::ui
