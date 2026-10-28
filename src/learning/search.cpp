/// @file
/// Offline-first provider coordination, independent of the terminal renderer.
#include "pawingo/learning/providers.hpp"
#include "pawingo/storage/store.hpp"
#include <stdexcept>
#include <utility>

namespace pawingo {
std::vector<Vocabulary> search_dictionaries(Store &store,
  const std::vector<DictionaryProvider *> &providers,
  const std::string &query,
  const std::string &meaning_language,
  const std::function<Consent(const DictionaryProvider &, const std::string &)> &ask) {
  if (query.empty())
    return {};
  std::vector<Vocabulary> results;
  auto collect = [&](DictionaryProvider &provider) {
    auto entries = provider.search(query, meaning_language);
    for (auto &entry : entries) {
      if (entry.source.empty())
        throw std::runtime_error("dictionary returned an entry without a source");
      results.push_back(std::move(entry));
    }
  };
  // Collect all local results before deciding whether network access is needed.
  for (auto *provider : providers)
    if (provider && !provider->online())
      collect(*provider);
  if (!results.empty())
    return results;
  for (auto *provider : providers) {
    if (!provider || !provider->online())
      continue;
    auto policy = store.policy(provider->id());
    if (policy == OnlinePolicy::offline)
      continue;
    if (policy == OnlinePolicy::ask) {
      if (!ask)
        continue;
      auto consent = ask(*provider, query);
      if (consent == Consent::deny)
        continue;
      if (consent == Consent::always)
        store.set_policy(provider->id(), OnlinePolicy::allow);
    }
    collect(*provider);
  }
  return results;
}
} // namespace pawingo
