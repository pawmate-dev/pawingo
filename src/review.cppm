module;
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

export module pawingo.review;

export namespace pawingo::review {
// Collapse ASCII whitespace only. Case and UTF-8 bytes remain unchanged;
// language plugins handle Unicode normalization and language-specific rules.
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
bool exact(std::string_view answer, std::string_view expected) {
  const auto a = normalize(answer);
  return !a.empty() && a == normalize(expected);
}
// Initial policy: retry after ten minutes, or double a one-day interval for
// each consecutive success (capped at 64 days). Times are Unix seconds.
std::int64_t next_review(std::int64_t now, bool correct, unsigned consecutive_correct) {
  if (!correct)
    return now + 600;
  constexpr std::int64_t day = 86400;
  auto shift = std::min(consecutive_correct, 6u);
  return now + (day << shift);
}
} // namespace pawingo::review
