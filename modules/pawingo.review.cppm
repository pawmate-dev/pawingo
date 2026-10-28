export module pawingo.review;

import std;
import pawingo.config;

export namespace pawingo::review {
/// Collapses ASCII whitespace while preserving case and UTF-8 bytes.
/// Unicode normalization and language-specific rules belong to the backend.
std::string normalize(std::string_view input) {
  std::string result;
  bool space = false;
  for (unsigned char c : input) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      space = !result.empty();
      continue;
    }
    if (space)
      result += ' ';
    space = false;
    result += static_cast<char>(c);
  }
  return result;
}

/// Matches nonempty answers after conservative whitespace normalization.
bool exact(std::string_view answer, std::string_view expected) {
  const auto a = normalize(answer);
  return !a.empty() && a == normalize(expected);
}

/// Computes the initial review policy's next Unix-second timestamp.
///
/// @param now Review completion time in Unix seconds.
/// @param correct Whether the submitted answer was accepted.
/// @param consecutive_correct Number of preceding consecutive successful reviews.
/// @return A timestamp computed from the embedded retry, initial interval and doubling limit.
/// This policy is independent of persistence; callers record the resulting timestamp.
std::int64_t next_review(std::int64_t now, bool correct, unsigned consecutive_correct) {
  if (!correct)
    return now + config::application().integer("review.retry_seconds", 1, 86400);
  const std::int64_t day =
    config::application().integer("review.initial_interval_seconds", 1, 31536000);
  auto shift = std::min(consecutive_correct,
    static_cast<unsigned>(config::application().integer("review.max_doublings", 0, 16)));
  return now + (day << shift);
}
} // namespace pawingo::review
