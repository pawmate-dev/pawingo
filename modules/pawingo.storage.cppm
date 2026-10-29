export module pawingo.storage;

import std;
export import pawingo.host;
export import pawingo.review;

/// Shared SQLite persistence, independent of language panels and the terminal renderer.
export namespace pawingo {
enum class OnlinePolicy { ask, allow, offline };

enum class ReviewStoreErrorKind { invalid_input, missing, stale, storage };

/// Typed review persistence failure; database diagnostics are for the frontend boundary only.
struct ReviewStoreError {
  ReviewStoreErrorKind kind;
  std::string message;
};

struct ReviewQueueRequest {
  std::chrono::sys_seconds now;
  std::size_t limit = 1;
  std::string language; ///< Empty selects all saved languages, including unavailable plugins.
};

/// Owns one SQLite connection. Persisted enum values form part of its schema.
class Store : public Host {
public:
  /// Opens the database and applies the idempotent schema; SQLite failures propagate.
  explicit Store(const std::string &path);
  ~Store() override;
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  /// Requires language, expression, meaning and source; duplicate entries are ignored.
  void save(const Vocabulary &entry) override;
  /// Returns saved entries in persistent ID order.
  std::vector<Vocabulary> vocabulary();
  /// Inserts or updates a resource; requires a nonempty ID and display name.
  void put_resource(const Resource &resource);
  /// Includes disabled resources so callers can manage them as well as use them.
  std::vector<Resource> resources(ResourceKind kind, const std::string &language) override;
  /// A service without a stored policy defaults to asking the user.
  OnlinePolicy policy(const std::string &service);
  /// Persists a service policy; requires a nonempty service ID.
  void set_policy(const std::string &service, OnlinePolicy policy);
  /// Returns review timestamps in the half-open Unix-second interval [from, until).
  std::vector<std::int64_t> review_timestamps(std::int64_t from, std::int64_t until);
  /// Appends a review for an existing entry, using Unix-second timestamps.
  /// SQLite foreign-key failures propagate; this method does not choose the next interval.
  void record_review(std::int64_t vocabulary_id,
    Match result,
    std::int64_t reviewed_at,
    std::int64_t next_review_at);
  /// Enrolls additional question types for an existing word without resetting any memory.
  /// Types are stable persisted IDs; invalid IDs/types return typed errors. No activity is written.
  std::expected<void, ReviewStoreError> enroll_review_cards(std::int64_t vocabulary_id,
    std::span<const review::QuestionType> types);
  /// Returns due snapshots, scheduled cards before new cards, then ordered by due time and ID.
  /// Limit must be 1..1000 and now must be nonnegative. No schedules or logs are changed.
  std::expected<std::vector<review::Card>, ReviewStoreError>
  review_queue(const ReviewQueueRequest &);
  /// Atomically compares the revision, updates memory/due time and writes both the FSRS event and
  /// its activity record. Failure rolls back all three. A repeated/stale submission cannot grade
  /// the card twice. The Store must be used from one owner thread; SQLite serializes connections.
  std::expected<void, ReviewStoreError> commit_review(const review::Commit &);
  /// Returns graded FSRS history in event-ID order; legacy unrated reviews remain activity only.
  std::expected<std::vector<review::Record>, ReviewStoreError>
  review_history(std::int64_t vocabulary_id);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pawingo
