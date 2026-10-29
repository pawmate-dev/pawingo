export module pawingo.types;

import std;

/// Frontend-neutral vocabulary, assessment, and resource data shared by language backends.
export namespace pawingo {
/// A sourced definition suitable for lookup, collection, and review.
struct Vocabulary {
  /// Language identifiers are supplied by the plugin; meaning_language identifies
  /// the language of the definition, which may differ from the studied language.
  std::string language;
  std::string expression; ///< Written form presented to the learner.
  std::string reading; ///< Language-specific pronunciation notation, such as kana.
  std::string meaning_language; ///< Language identifier of the definition.
  std::string meaning; ///< Definition text preserved when saving the entry.
  std::string source; ///< Provider identity and optional stable entry identifier.
  std::string context; ///< Optional source sentence from a reading session.
  /// Assigned by the store; newly constructed entries can leave this at zero.
  std::int64_t id = 0;
};

/// One committed vocabulary review used for calendar statistics, independent of question type.
/// Definitions and sources are excluded from word identity; language, spelling and reading remain.
struct LearningActivity {
  std::int64_t reviewed_at;
  std::string language, expression, reading;
};

/// "acceptable" lets a plugin distinguish a tolerated variant from an exact answer.
enum class Match { correct, acceptable, incorrect };

/// Assessment outcome and optional explanation for a learner's submitted answer.
struct MatchResult {
  Match result = Match::incorrect;
  std::string feedback;
};

/// Resource capabilities managed independently from installed language plugins.
enum class ResourceKind { dictionary, tts, translation };

/// A user-configured resource; location interpretation belongs to its provider.
struct Resource {
  std::string id;
  std::string name;
  ResourceKind kind;
  std::string language;
  std::string location;
  bool enabled = true;
  /// Larger priorities appear first in the host's resource list.
  int priority = 0;
};

} // namespace pawingo
