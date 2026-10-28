export module pawingo.learning;

import std;
export import pawingo.storage;

/// Provider contracts and offline-first orchestration.
export namespace pawingo {
/// Only "always" changes the persisted policy; "once" applies to this request.
enum class Consent { deny, once, always };

/// Local or remote dictionary capability consumed by the shared coordinator.
class DictionaryProvider {
public:
  virtual ~DictionaryProvider() = default;
  /// Returns the stable service ID used for authorization and resource selection.
  virtual std::string id() const = 0;
  /// Returns the provider name shown in confirmation prompts.
  virtual std::string name() const = 0;
  /// Returns whether lookup requires network access.
  virtual bool online() const = 0;
  /// Returns sourced definitions in the requested language, or an empty result.
  virtual std::vector<Vocabulary> search(const std::string &query,
    const std::string &meaning_language) = 0;
};

/// Local providers are queried first. Online providers are consulted only when
/// no local result exists and the corresponding service policy permits it. The
/// callback receives the provider and query; without a callback, ask means deny.
/// Provider failures propagate to the caller and results must include a source.
///
/// @param store Persisted per-service authorization policies.
/// @param providers Borrowed providers; local providers are queried before remote ones.
/// @param query Search text; empty input performs no lookup.
/// @param meaning_language Requested language of definitions.
/// @param ask UI consent callback; an absent callback denies services with ask policy.
/// @return Combined local results, or authorized remote results when all local results are empty.
/// @throws std::runtime_error If a provider omits source information; provider failures propagate.
///
/// Providers and store must outlive this synchronous call. Only persistent consent changes policy.
std::vector<Vocabulary> search_dictionaries(Store &store,
  const std::vector<DictionaryProvider *> &providers,
  const std::string &query,
  const std::string &meaning_language,
  const std::function<Consent(const DictionaryProvider &, const std::string &)> &ask);

/// Optional pronunciation capability; blocking implementations must run on a worker.
class SpeechProvider {
public:
  virtual ~SpeechProvider() = default;
  /// Returns the stable service ID used for authorization and resource selection.
  virtual std::string id() const = 0;
  /// Speaks language-tagged text; provider failures propagate to the caller.
  virtual void speak(const std::string &language, const std::string &text) = 0;
};

} // namespace pawingo
