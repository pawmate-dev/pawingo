#pragma once
#include "pawingo/host.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

/// Shared SQLite persistence, independent of language panels and the terminal renderer.
namespace pawingo {
enum class OnlinePolicy { ask, allow, offline };

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

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pawingo
