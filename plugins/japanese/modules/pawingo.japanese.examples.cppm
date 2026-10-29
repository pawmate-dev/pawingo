export module pawingo.japanese.examples;

import std;
export import pawingo.japanese.dictionary;

export namespace pawingo::japanese {
/// One independently sourced sentence and an optional translation in the requested language.
struct Example {
  std::int64_t sentence_id = 0, translation_id = 0;
  std::string text, translation, translation_language;
  std::string source_url, translation_url, attribution, license;
  bool checked = false; ///< Upstream's checked usage marker, not a claim of perfect translation.
  bool sense_link = false;
};

enum class ExampleErrorKind { invalid_input, format, io, storage, cancelled };

/// Recoverable corpus failure; presentation belongs to the frontend.
struct ExampleError {
  ExampleErrorKind kind;
  std::string message;
};

/// Selects a dictionary meaning without inferring identity from its display label.
struct ExampleRequest {
  EntryReference entry;
  std::string expression, reading, target;
};

/// Four-column Tatoeba TSV: Japanese ID, Japanese text, translation ID, translation text.
struct TranslationImport {
  std::filesystem::path file;
  std::string language; ///< User-selected target language; never inferred from the file name.
};

/// Returns the managed, independent SQLite example resource path.
std::filesystem::path examples_path();

/// Offline indexed corpus. Construction does not open files or perform network requests.
/// Operations serialize per instance; use one shared owner per backend. Imports stage a full
/// candidate database and atomically replace the resource after validation. Concurrent imports
/// from separate processes/instances are unsupported. Destruction requires callers to finish.
class Examples {
public:
  explicit Examples(std::filesystem::path path = examples_path());
  ~Examples();
  Examples(const Examples &) = delete;
  Examples &operator=(const Examples &) = delete;
  /// Returns the immutable resource path owned by this service.
  const std::filesystem::path &path() const;
  /// Matches explicit entry IDs or exact indexed headwords, applying reading/sense restrictions.
  /// Ambiguous bare headwords are excluded. Checked usages rank first; results have stable order.
  /// No corpus returns an empty list. Missing target translations remain empty, without fallback.
  std::expected<std::vector<Example>, ExampleError> find(const ExampleRequest &,
    std::stop_token stop = {});
  /// Imports plain/gzip WWWJDIC five-column TSV. Replaces indexed sentences/English translations
  /// while preserving separately imported target-language translations. Cancellation or malformed
  /// data before commit preserves the installed corpus and removes staged files. Returns pair
  /// count.
  std::expected<std::size_t, ExampleError> install_index(const std::filesystem::path &,
    std::stop_token stop = {});
  /// Imports plain/gzip four-column sentence pairs into an installed index, replacing only that
  /// translation language. Japanese IDs and text must agree with the indexed corpus. Rows whose
  /// Japanese IDs are absent are skipped. Failure preserves every installed resource.
  std::expected<std::size_t, ExampleError> install_translations(const TranslationImport &,
    std::stop_token stop = {});

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace pawingo::japanese
