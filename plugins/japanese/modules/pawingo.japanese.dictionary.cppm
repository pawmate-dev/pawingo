export module pawingo.japanese.dictionary;

import std;
export import pawingo.types;

/// Japanese dictionary parsing and lookup; no terminal, plugin-loader, or UI dependencies.
export namespace pawingo::japanese {
/// Returns the managed JMdict location using XDG_DATA_HOME or HOME.
std::filesystem::path dictionary_path();
/// Returns existing environment-override and managed dictionaries in lookup order.
std::vector<std::filesystem::path> dictionary_paths();
/// Maps a target language or locale to JMdict's three-letter gloss identifier.
std::string gloss_language(const std::string &language);

/// Owns shared immutable dictionary snapshots, refreshed when source metadata changes.
class Dictionary {
public:
  /// Creates an empty cache; dictionary files are not opened until requested.
  Dictionary();
  ~Dictionary();
  Dictionary(const Dictionary &) = delete;
  Dictionary &operator=(const Dictionary &) = delete;
  /// Looks up Japanese forms, kana readings, or meaning text in a requested language.
  ///
  /// @param query Search text; ordinary katakana is folded to hiragana.
  /// @param target Definition language or locale, normalized through gloss_language().
  /// @param paths Existing JMdict XML/gzip files in provider preference order.
  /// @param stop Cooperative cancellation for parsing and scanning.
  /// @return Up to the configured maximum of sourced entries, with exact forms ahead of prefixes
  /// and meanings.
  /// @throws std::runtime_error For cancellation, unreadable files, or invalid JMdict data.
  ///
  /// No network fallback or target-language substitution occurs. Empty input returns no
  /// results. File parsing runs on demand; callers should schedule this work off the UI thread.
  std::vector<Vocabulary> search(const std::string &query,
    const std::string &target,
    const std::vector<std::filesystem::path> &paths,
    std::stop_token stop = {});

  /// Validates a private copy of a dictionary before committing it to managed storage.
  ///
  /// @param source Plain XML or gzip input file; the source is not modified.
  /// @param stop Cancellation checked before copying, during parsing, and before commit.
  /// @return Number of usable spelling/reading/sense/language records installed.
  /// @throws std::runtime_error For malformed, oversized, or cancelled imports.
  /// @throws std::filesystem::filesystem_error For copy, rename, or metadata failures.
  ///
  /// Failure before commit preserves the installed file and removes the temporary copy.
  /// The parsed snapshot is reused by subsequent lookups in this Dictionary instance.
  std::size_t install(const std::filesystem::path &source, std::stop_token stop = {});

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace pawingo::japanese
