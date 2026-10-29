module pawingo.learning;

import std;

namespace pawingo {
std::map<std::string, std::size_t> learning_word_counts(std::span<const LearningActivity> events,
  const std::function<std::string(std::int64_t)> &date_key) {
  if (!date_key)
    throw std::invalid_argument("Learning statistics require a date-key callback.");
  using Word = std::tuple<std::string, std::string, std::string>;
  std::map<std::string, std::set<Word>> words;
  for (const auto &event : events)
    words[date_key(event.reviewed_at)].emplace(event.language, event.expression, event.reading);
  std::map<std::string, std::size_t> counts;
  for (const auto &[date, learned] : words)
    counts.emplace(date, learned.size());
  return counts;
}
} // namespace pawingo
