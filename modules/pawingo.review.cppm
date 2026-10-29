export module pawingo.review;

import std;
export import pawingo.types;

export namespace pawingo::review {
/// Collapses ASCII whitespace while preserving case and UTF-8 bytes.
/// Unicode normalization and language-specific rules belong to the backend.
std::string normalize(std::string_view input);

/// Matches nonempty answers after conservative whitespace normalization.
bool exact(std::string_view answer, std::string_view expected);
/// Matches a complete saved meaning or one semicolon-delimited alternative after ASCII case and
/// whitespace normalization. Does not guess synonyms, strip parentheses or accept substrings.
MatchResult match_meaning(const Vocabulary &, std::string_view answer);

/// Fractional days returned by FSRS; never implicitly rounded to Unix seconds or calendar dates.
using Days = std::chrono::duration<float, std::ratio<86400>>;

/// Recall rating, stored independently of a language backend's answer-matching result.
enum class Rating : std::uint32_t { again = 1, hard = 2, good = 3, easy = 4 };

/// Persistent model state for one learning card, independent of vocabulary spelling or language.
struct MemoryState {
  Days stability;
  float difficulty;
  bool operator==(const MemoryState &) const = default;
};

/// Post-rating memory state and the algorithm's unrounded interval, without queue policy.
struct ReviewState {
  MemoryState memory;
  Days interval;
};

/// Four alternatives; callers commit only the one corresponding to the final recall rating.
struct NextStates {
  ReviewState again, hard, good, easy;
};

/// Snapshot of one saved meaning card. New cards have neither memory nor a previous review.
struct Card {
  Vocabulary word;
  std::optional<MemoryState> memory;
  std::optional<std::chrono::sys_seconds> last_review;
  std::chrono::sys_seconds due{};
  std::int64_t revision = 0;
};

/// Host scheduling policy applied after FSRS; memory updates still come from the selected rating.
struct Outcome {
  MemoryState memory;
  std::chrono::seconds interval;
  std::chrono::sys_seconds due;
};

/// Immutable-by-convention preview; persistence validates the snapshot revision before committing.
struct Plan {
  std::int64_t vocabulary_id, revision;
  std::chrono::sys_seconds reviewed_at;
  std::chrono::days elapsed;
  float desired_retention;
  std::array<Outcome, 4> outcomes; ///< Again, Hard, Good and Easy in stable rating order.
};

/// One explicit grading action; answer matching and recall rating remain independent.
struct Commit {
  Plan plan;
  Rating rating;
  Match result;
  std::string answer;
};

/// Auditable FSRS event joined to its legacy-compatible activity record.
struct Record {
  std::int64_t id, vocabulary_id, revision;
  Rating rating;
  Match result;
  std::string answer;
  std::chrono::sys_seconds reviewed_at, due;
  std::chrono::days elapsed;
  MemoryState memory;
  float desired_retention;
};

/// Scheduling input. A new card has no memory and zero elapsed days.
struct ScheduleRequest {
  std::optional<MemoryState> memory;
  std::chrono::days elapsed{};
  /// Omit to use the embedded default; supplied retention must be finite and strictly in (0, 1).
  std::optional<float> desired_retention;
};

/// One chronological review; the first review of a new card has zero elapsed days.
struct Review {
  Rating rating;
  std::chrono::days elapsed;
};

/// Typed failures crossing the native algorithm boundary, without string-based classification.
enum class ErrorKind { invalid_input, invalid_parameters, backend, panic, configuration };

/// A recoverable failure; messages are diagnostic English, ready for frontend presentation.
struct Error {
  ErrorKind kind;
  std::string message;
};

/// Owns an immutable FSRS model through the private Rust C ABI.
/// Calls on a live instance may run concurrently. Destruction or moving requires all callers to
/// finish first. No network, persistence or TUI state is accessed during scheduling or replay.
class Scheduler {
public:
  /// Creates a model with upstream defaults for an empty parameter span, or custom parameters.
  /// Parameters are copied during construction; retention defaults come from embedded YAML.
  /// Invalid parameters/configuration and contained Rust panics return typed errors.
  static std::expected<Scheduler, Error> create(std::span<const float> parameters = {});
  ~Scheduler();
  Scheduler(Scheduler &&) noexcept;
  Scheduler &operator=(Scheduler &&) noexcept;
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

  /// Computes all four outcomes without changing the supplied memory or model.
  /// Elapsed days must fit uint32_t. Invalid states/retention, moved-from instances and native
  /// failures return errors. Intervals retain fractional days; learning steps, rounding, maximum
  /// intervals and due timestamps belong to the review queue, not this algorithm adapter.
  std::expected<NextStates, Error> next_states(const ScheduleRequest &) const;

  /// Rebuilds memory from nonempty chronological history, optionally after a persisted state.
  /// With no starting state the first elapsed interval must be zero. Ratings must be 1 through 4,
  /// and each elapsed interval must fit uint32_t. At most one million records may be replayed.
  /// Inputs are borrowed only for the call; invalid histories and native failures return errors.
  std::expected<MemoryState, Error> memory_state(std::span<const Review> history,
    std::optional<MemoryState> starting = {}) const;

private:
  struct Impl;
  explicit Scheduler(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

/// Plans a due meaning card at a Unix-second completion time without mutating it.
/// Whole elapsed 24-hour days are passed to FSRS; intervals for Hard/Good/Easy are rounded and
/// bounded by embedded policy, while Again uses the configured short relearning delay. Rejects
/// future-due cards, backward clocks, inconsistent snapshots and timestamp overflow. The caller
/// commits one outcome atomically with the answer and rating through Store::commit_review.
std::expected<Plan, Error>
plan_review(const Scheduler &, const Card &, std::chrono::sys_seconds now);
} // namespace pawingo::review
