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
} // namespace pawingo::review
