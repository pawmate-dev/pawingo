module;
#include "pawingo_fsrs.h"

module pawingo.review;

import std;
import pawingo.config;

namespace pawingo::review {
namespace {
Error error(std::uint32_t status) {
  switch (status) {
  case PAWINGO_FSRS_INVALID_INPUT:
    return {ErrorKind::invalid_input, "Invalid FSRS memory, rating, retention or elapsed days."};
  case PAWINGO_FSRS_INVALID_PARAMETERS:
    return {ErrorKind::invalid_parameters, "Unsupported or non-finite FSRS parameters."};
  case PAWINGO_FSRS_PANIC:
    return {ErrorKind::panic, "FSRS encountered an unexpected internal panic."};
  default:
    return {ErrorKind::backend, "FSRS could not produce a valid model result."};
  }
}

bool valid_elapsed(std::chrono::days elapsed) {
  return elapsed.count() >= 0 && elapsed.count() <= std::numeric_limits<std::uint32_t>::max();
}

PawingoFsrsMemory native(const MemoryState &memory) {
  return {memory.stability.count(), memory.difficulty};
}

MemoryState memory(const PawingoFsrsMemory &value) {
  return {Days(value.stability), value.difficulty};
}

ReviewState state(const PawingoFsrsState &value) {
  return {memory(value.memory), Days(value.interval_days)};
}
} // namespace

struct Scheduler::Impl {
  std::unique_ptr<PawingoFsrs, decltype(&pawingo_fsrs_v1_free)> model{nullptr,
    pawingo_fsrs_v1_free};
  float retention;
};

Scheduler::Scheduler(std::unique_ptr<Impl> impl)
  : impl_(std::move(impl)) {
}

Scheduler::~Scheduler() = default;
Scheduler::Scheduler(Scheduler &&) noexcept = default;
Scheduler &Scheduler::operator=(Scheduler &&) noexcept = default;

std::expected<Scheduler, Error> Scheduler::create(std::span<const float> parameters) {
  auto impl = std::make_unique<Impl>();
  try {
    impl->retention = config::application().real("review.desired_retention", 0.F, 1.F);
    if (impl->retention == 0.F || impl->retention == 1.F)
      return std::unexpected(Error{ErrorKind::configuration,
        "FSRS desired retention must be strictly between zero and one."});
  } catch (const std::exception &e) {
    return std::unexpected(Error{ErrorKind::configuration, e.what()});
  }
  PawingoFsrs *model = nullptr;
  const auto status = pawingo_fsrs_v1_new(parameters.data(), parameters.size(), &model);
  if (status != PAWINGO_FSRS_OK)
    return std::unexpected(error(status));
  impl->model.reset(model);
  return Scheduler(std::move(impl));
}

std::expected<NextStates, Error> Scheduler::next_states(const ScheduleRequest &request) const {
  if (!impl_ || !valid_elapsed(request.elapsed))
    return std::unexpected(error(PAWINGO_FSRS_INVALID_INPUT));
  PawingoFsrsMemory previous{};
  if (request.memory)
    previous = native(*request.memory);
  PawingoFsrsNext result{};
  const auto status = pawingo_fsrs_v1_next(impl_->model.get(),
    request.memory ? &previous : nullptr,
    request.desired_retention.value_or(impl_->retention),
    static_cast<std::uint32_t>(request.elapsed.count()),
    &result);
  if (status != PAWINGO_FSRS_OK)
    return std::unexpected(error(status));
  return NextStates{state(result.states[0]),
    state(result.states[1]),
    state(result.states[2]),
    state(result.states[3])};
}

std::expected<MemoryState, Error> Scheduler::memory_state(std::span<const Review> history,
  std::optional<MemoryState> starting) const {
  if (!impl_ || history.empty() || history.size() > 1000000)
    return std::unexpected(error(PAWINGO_FSRS_INVALID_INPUT));
  std::vector<PawingoFsrsReview> reviews;
  reviews.reserve(history.size());
  for (const auto &review : history) {
    if (!valid_elapsed(review.elapsed))
      return std::unexpected(error(PAWINGO_FSRS_INVALID_INPUT));
    reviews.push_back({static_cast<std::uint32_t>(review.rating),
      static_cast<std::uint32_t>(review.elapsed.count())});
  }
  PawingoFsrsMemory previous{}, result{};
  if (starting)
    previous = native(*starting);
  const auto status = pawingo_fsrs_v1_memory(impl_->model.get(),
    reviews.data(),
    reviews.size(),
    starting ? &previous : nullptr,
    &result);
  if (status != PAWINGO_FSRS_OK)
    return std::unexpected(error(status));
  return memory(result);
}

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

MatchResult match_meaning(const Vocabulary &word, std::string_view answer) {
  auto key = [](std::string_view value) {
    auto result = normalize(value);
    for (char &c : result)
      if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    return result;
  };
  const auto submitted = key(answer);
  if (submitted.empty())
    return {Match::incorrect, {}};
  auto matches = [&](std::string_view expected) -> std::optional<MatchResult> {
    if (submitted != key(expected))
      return {};
    return MatchResult{answer == normalize(expected) ? Match::correct : Match::acceptable, {}};
  };
  if (auto full = matches(word.meaning))
    return *full;
  std::string_view remaining(word.meaning);
  for (;;) {
    const auto end = remaining.find(';');
    if (auto alternative = matches(remaining.substr(0, end)))
      return *alternative;
    if (end == std::string_view::npos)
      break;
    remaining.remove_prefix(end + 1);
  }
  return {Match::incorrect, {}};
}

std::pair<std::vector<AnswerSpan>, std::vector<AnswerSpan>>
answer_difference(std::string_view submitted, std::string_view expected) {
  auto glyphs = [](std::string_view text) {
    std::vector<std::string_view> result;
    for (std::size_t i = 0; i < text.size();) {
      const auto start = i++;
      while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        ++i;
      result.push_back(text.substr(start, i - start));
    }
    return result;
  };
  const auto a = glyphs(submitted), b = glyphs(expected);
  std::size_t prefix = 0, suffix = 0;
  while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
    ++prefix;
  while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
    a[a.size() - suffix - 1] == b[b.size() - suffix - 1])
    ++suffix;
  auto spans = [&](const auto &units) {
    std::vector<AnswerSpan> result;
    for (std::size_t i = 0; i < units.size(); ++i) {
      const bool changed = i >= prefix && i < units.size() - suffix;
      if (result.empty() || result.back().changed != changed)
        result.push_back({std::string(units[i]), changed});
      else
        result.back().text += units[i];
    }
    return result;
  };
  return {spans(a), spans(b)};
}

std::expected<Plan, Error>
plan_review(const Scheduler &scheduler, const Card &card, std::chrono::sys_seconds now) {
  auto invalid = [] {
    return std::unexpected(Error{ErrorKind::invalid_input,
      "Invalid or future-due review snapshot."});
  };
  if (static_cast<int>(card.kind) < 0 || static_cast<int>(card.kind) > 2 || card.word.id <= 0 ||
    card.revision < 0 || card.revision == std::numeric_limits<std::int64_t>::max() ||
    now.time_since_epoch().count() < 0 || card.due.time_since_epoch().count() < 0 ||
    now < card.due || card.memory.has_value() != card.last_review.has_value())
    return invalid();
  std::chrono::days elapsed{};
  if (card.last_review) {
    if (card.last_review->time_since_epoch().count() < 0 || now < *card.last_review ||
      card.due < *card.last_review)
      return invalid();
    elapsed = std::chrono::duration_cast<std::chrono::days>(now - *card.last_review);
  }
  try {
    struct Policy {
      int again, minimum, maximum;
      float retention;
    };

    static const auto policy = [] {
      const auto &config = pawingo::config::application();
      Policy value{config.integer("review.again_seconds", 1, 86400),
        config.integer("review.minimum_interval_days", 1, 3650000),
        config.integer("review.maximum_interval_days", 1, 3650000),
        config.real("review.desired_retention", 0.F, 1.F)};
      if (value.minimum > value.maximum || value.retention <= 0.F || value.retention >= 1.F)
        throw std::runtime_error("Invalid review interval or retention policy.");
      return value;
    }();
    auto next = scheduler.next_states({card.memory, elapsed, policy.retention});
    if (!next)
      return std::unexpected(next.error());
    Plan result{card.word.id, card.revision, now, elapsed, policy.retention, {}};
    result.kind = card.kind;
    const std::array states{next->again, next->hard, next->good, next->easy};
    for (std::size_t i = 0; i < states.size(); ++i) {
      const auto days = static_cast<std::int64_t>(
        std::round(std::clamp(static_cast<double>(states[i].interval.count()),
          double(policy.minimum),
          double(policy.maximum)))
      );
      const auto seconds = i == 0 ? std::int64_t(policy.again) : days * 86400;
      if (now.time_since_epoch().count() > std::numeric_limits<std::int64_t>::max() - seconds)
        return invalid();
      result.outcomes[i] = {states[i].memory,
        std::chrono::seconds(seconds),
        now + std::chrono::seconds(seconds)};
    }
    return result;
  } catch (const std::exception &e) {
    return std::unexpected(Error{ErrorKind::configuration, e.what()});
  }
}
} // namespace pawingo::review
