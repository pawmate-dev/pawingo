#pragma once
#include "pawingo/plugin.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pawingo {
enum class OnlinePolicy { ask, allow, offline };
// Owns one SQLite connection. Persisted enum values form part of its schema.
class Store : public Host {
public:
  explicit Store(const std::string &path);
  ~Store() override;
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  // Requires language, expression, meaning and source; duplicate entries are ignored.
  void save(const Vocabulary &entry) override;
  std::vector<Vocabulary> vocabulary();
  void put_resource(const Resource &resource);
  // Includes disabled resources so callers can manage them as well as use them.
  std::vector<Resource> resources(ResourceKind kind, const std::string &language) override;
  // A service without a stored policy defaults to asking the user.
  OnlinePolicy policy(const std::string &service);
  void set_policy(const std::string &service, OnlinePolicy policy);
  // Times are Unix timestamps in seconds; vocabulary_id must refer to a saved entry.
  void record_review(std::int64_t vocabulary_id,
    Match result,
    std::int64_t reviewed_at,
    std::int64_t next_review_at);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Only "always" changes the persisted policy; "once" applies to this request.
enum class Consent { deny, once, always };
class DictionaryProvider {
public:
  virtual ~DictionaryProvider() = default;
  virtual std::string id() const = 0;
  virtual std::string name() const = 0;
  virtual bool online() const = 0;
  virtual std::vector<Vocabulary> search(const std::string &query,
    const std::string &meaning_language) = 0;
};
// Local providers are queried first. Online providers are consulted only when
// no local result exists and the corresponding service policy permits it. The
// callback receives the provider and query; without a callback, ask means deny.
// Provider failures propagate to the caller and results must include a source.
std::vector<Vocabulary> search_dictionaries(Store &store,
  const std::vector<DictionaryProvider *> &providers,
  const std::string &query,
  const std::string &meaning_language,
  const std::function<Consent(const DictionaryProvider &, const std::string &)> &ask);

class SpeechProvider {
public:
  virtual ~SpeechProvider() = default;
  virtual std::string id() const = 0;
  virtual void speak(const std::string &language, const std::string &text) = 0;
};

class Plugins {
public:
  Plugins();
  ~Plugins();
  Plugins(const Plugins &) = delete;
  Plugins &operator=(const Plugins &) = delete;
  // load throws on failure; discover records individual errors and continues.
  void load(const std::string &path);
  void discover(const std::string &directory);
  // Pointers are borrowed and remain valid only while this registry is alive.
  std::vector<LanguagePlugin *> languages() const;
  const std::vector<std::string> &errors() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace pawingo
