module;
#include <sqlite_orm/sqlite_orm.h>

module pawingo.storage;

import std;
import pawingo.config;
import pawingo.assets.sql;

namespace pawingo {
namespace {
using namespace sqlite_orm;

template <typename Storage, typename Expression>
void execute_insert(Storage &storage, Expression expression) {
  auto statement = storage.prepare(std::move(expression));
  storage.execute(statement);
}

struct ResourceRow {
  std::string id, name;
  int kind;
  std::string language, location;
  int enabled, priority;
};

struct PolicyRow {
  std::string service;
  int policy;
};

struct ReviewRow {
  std::int64_t id, vocabulary_id;
  int result;
  std::int64_t reviewed_at, next_review_at;
};

struct CardRow {
  std::int64_t vocabulary_id;
  std::optional<double> stability, difficulty;
  std::optional<std::int64_t> last_review;
  std::int64_t due, revision;
};

struct FsrsRow {
  std::int64_t review_id, vocabulary_id, revision;
  int rating;
  std::string answer;
  std::int64_t elapsed_days;
  double stability, difficulty, retention;
};

struct ReviewFailure {
  ReviewStoreError error;
};

[[noreturn]] void reject_review(ReviewStoreErrorKind kind, std::string message) {
  throw ReviewFailure{{kind, std::move(message)}};
}

template <class F>
auto review_operation(F operation) -> std::expected<decltype(operation()), ReviewStoreError> {
  try {
    if constexpr (std::is_void_v<decltype(operation())>) {
      operation();
      return {};
    } else
      return operation();
  } catch (const ReviewFailure &e) {
    return std::unexpected(e.error);
  } catch (const std::exception &e) {
    return std::unexpected(ReviewStoreError{ReviewStoreErrorKind::storage, e.what()});
  }
}

auto make_database(const std::string &path) {
  return make_storage(path,
    make_table("vocabulary",
      make_column("id", &Vocabulary::id, primary_key()),
      make_column("language", &Vocabulary::language),
      make_column("expression", &Vocabulary::expression),
      make_column("reading", &Vocabulary::reading),
      make_column("meaning_language", &Vocabulary::meaning_language),
      make_column("meaning", &Vocabulary::meaning),
      make_column("source", &Vocabulary::source),
      make_column("context", &Vocabulary::context)),
    make_table("resources",
      make_column("id", &ResourceRow::id, primary_key()),
      make_column("name", &ResourceRow::name),
      make_column("kind", &ResourceRow::kind),
      make_column("language", &ResourceRow::language),
      make_column("location", &ResourceRow::location),
      make_column("enabled", &ResourceRow::enabled),
      make_column("priority", &ResourceRow::priority)),
    make_table("online_policy",
      make_column("service", &PolicyRow::service, primary_key()),
      make_column("policy", &PolicyRow::policy)),
    make_table("reviews",
      make_column("id", &ReviewRow::id, primary_key()),
      make_column("vocabulary_id", &ReviewRow::vocabulary_id),
      make_column("result", &ReviewRow::result),
      make_column("reviewed_at", &ReviewRow::reviewed_at),
      make_column("next_review_at", &ReviewRow::next_review_at)),
    make_table("review_cards",
      make_column("vocabulary_id", &CardRow::vocabulary_id, primary_key()),
      make_column("stability", &CardRow::stability),
      make_column("difficulty", &CardRow::difficulty),
      make_column("last_review_at", &CardRow::last_review),
      make_column("due_at", &CardRow::due),
      make_column("revision", &CardRow::revision)),
    make_table("fsrs_reviews",
      make_column("review_id", &FsrsRow::review_id, primary_key()),
      make_column("vocabulary_id", &FsrsRow::vocabulary_id),
      make_column("revision", &FsrsRow::revision),
      make_column("rating", &FsrsRow::rating),
      make_column("answer", &FsrsRow::answer),
      make_column("elapsed_days", &FsrsRow::elapsed_days),
      make_column("stability", &FsrsRow::stability),
      make_column("difficulty", &FsrsRow::difficulty),
      make_column("desired_retention", &FsrsRow::retention)));
}
} // namespace

struct Store::Impl {
  struct Storage : decltype(make_database("")) {
    using Base = decltype(make_database(""));
    explicit Storage(const std::string &path)
      : Base(make_database(path)) {
    }
    using Base::get_connection;
  };

  Storage db;

  explicit Impl(const std::string &path)
    : db(path) {
    auto initialize = [](sqlite3 *connection) {
      char *error = nullptr;
      const char *migrations[] = {sql::schema, sql::review};
      for (const auto *migration : migrations) {
        if (sqlite3_exec(connection, migration, nullptr, nullptr, &error) != SQLITE_OK) {
          std::string message = error ? error : sqlite3_errmsg(connection);
          sqlite3_free(error);
          throw std::runtime_error(message);
        }
      }
    };
    db.on_open = initialize;
    db.open_forever();
    // In-memory connections open during construction, before on_open is set.
    if (path.empty() || path == ":memory:")
      initialize(db.get_connection().get());
  }
};

Store::Store(const std::string &path)
  : impl_(std::make_unique<Impl>(path)) {
}

Store::~Store() = default;

void Store::save(const Vocabulary &v) {
  if (v.language.empty() || v.expression.empty() || v.meaning.empty() || v.source.empty())
    throw std::invalid_argument("vocabulary requires language, expression, meaning and source");
  using namespace sqlite_orm;
  impl_->db.transaction([&] {
    execute_insert(impl_->db,
      insert(or_ignore(),
        into<Vocabulary>(),
        columns(&Vocabulary::language,
          &Vocabulary::expression,
          &Vocabulary::reading,
          &Vocabulary::meaning_language,
          &Vocabulary::meaning,
          &Vocabulary::source,
          &Vocabulary::context),
        values(std::make_tuple(v.language,
          v.expression,
          v.reading,
          v.meaning_language,
          v.meaning,
          v.source,
          v.context))));
    const auto ids = impl_->db.select(&Vocabulary::id,
      where(c(&Vocabulary::language) == v.language && c(&Vocabulary::expression) == v.expression &&
        c(&Vocabulary::reading) == v.reading &&
        c(&Vocabulary::meaning_language) == v.meaning_language &&
        c(&Vocabulary::meaning) == v.meaning && c(&Vocabulary::source) == v.source &&
        c(&Vocabulary::context) == v.context));
    if (ids.empty())
      throw std::runtime_error("Saved vocabulary could not be enrolled.");
    execute_insert(impl_->db,
      insert(or_ignore(),
        into<CardRow>(),
        columns(&CardRow::vocabulary_id, &CardRow::due, &CardRow::revision),
        values(std::make_tuple(ids.front(), 0, 0))));
    return true;
  });
}

std::vector<Vocabulary> Store::vocabulary() {
  return impl_->db.get_all<Vocabulary>(sqlite_orm::order_by(&Vocabulary::id));
}

void Store::put_resource(const Resource &r) {
  if (r.id.empty() || r.name.empty())
    throw std::invalid_argument("resource requires id and name");
  using namespace sqlite_orm;
  execute_insert(impl_->db,
    insert(into<ResourceRow>(),
      columns(&ResourceRow::id,
        &ResourceRow::name,
        &ResourceRow::kind,
        &ResourceRow::language,
        &ResourceRow::location,
        &ResourceRow::enabled,
        &ResourceRow::priority),
      values(std::make_tuple(r.id,
        r.name,
        static_cast<int>(r.kind),
        r.language,
        r.location,
        int(r.enabled),
        r.priority)),
      on_conflict(&ResourceRow::id)
        .do_update(set(c(&ResourceRow::name) = excluded(&ResourceRow::name),
          c(&ResourceRow::kind) = excluded(&ResourceRow::kind),
          c(&ResourceRow::language) = excluded(&ResourceRow::language),
          c(&ResourceRow::location) = excluded(&ResourceRow::location),
          c(&ResourceRow::enabled) = excluded(&ResourceRow::enabled),
          c(&ResourceRow::priority) = excluded(&ResourceRow::priority)))));
}

std::vector<Resource> Store::resources(ResourceKind kind, const std::string &language) {
  using namespace sqlite_orm;
  auto rows = impl_->db.get_all<ResourceRow>(
    where(c(&ResourceRow::kind) == static_cast<int>(kind) && c(&ResourceRow::language) == language),
    multi_order_by(order_by(&ResourceRow::priority).desc(), order_by(&ResourceRow::id))
  );
  std::vector<Resource> result;
  for (const auto &r : rows)
    result.push_back({r.id, r.name, kind, r.language, r.location, r.enabled != 0, r.priority});
  return result;
}

OnlinePolicy Store::policy(const std::string &service) {
  auto row = impl_->db.get_pointer<PolicyRow>(service);
  if (!row) {
    const auto policy = config::application().text("services.default_policy");
    if (policy == "ask")
      return OnlinePolicy::ask;
    if (policy == "allow")
      return OnlinePolicy::allow;
    if (policy == "offline")
      return OnlinePolicy::offline;
    throw std::runtime_error("Unknown default online-service policy.");
  }
  return row->policy == 1 ? OnlinePolicy::allow
    : row->policy == 2    ? OnlinePolicy::offline
                          : OnlinePolicy::ask;
}

void Store::set_policy(const std::string &service, OnlinePolicy p) {
  if (service.empty())
    throw std::invalid_argument("service id is empty");
  using namespace sqlite_orm;
  execute_insert(impl_->db,
    insert(into<PolicyRow>(),
      columns(&PolicyRow::service, &PolicyRow::policy),
      values(std::make_tuple(service, static_cast<int>(p))),
      on_conflict(&PolicyRow::service)
        .do_update(set(c(&PolicyRow::policy) = excluded(&PolicyRow::policy)))));
}

void Store::record_review(std::int64_t id, Match result, std::int64_t reviewed, std::int64_t next) {
  impl_->db.insert(ReviewRow{0, id, static_cast<int>(result), reviewed, next});
}

std::vector<std::int64_t> Store::review_timestamps(std::int64_t from, std::int64_t until) {
  using namespace sqlite_orm;
  return impl_->db.select(&ReviewRow::reviewed_at,
    where(c(&ReviewRow::reviewed_at) >= from && c(&ReviewRow::reviewed_at) < until),
    order_by(&ReviewRow::reviewed_at));
}

std::expected<std::vector<review::Card>, ReviewStoreError>
Store::review_queue(const ReviewQueueRequest &request) {
  return review_operation([&] {
    if (request.now.time_since_epoch().count() < 0 || request.limit == 0 || request.limit > 1000)
      reject_review(ReviewStoreErrorKind::invalid_input,
        "Invalid review queue time or batch limit.");
    auto load = [&](auto condition) {
      return impl_->db.get_all<CardRow>(where(condition),
        multi_order_by(order_by(is_null(&CardRow::last_review)),
          order_by(&CardRow::due),
          order_by(&CardRow::vocabulary_id)),
        limit(static_cast<int>(request.limit)));
    };
    auto rows = request.language.empty()
      ? load(c(&CardRow::due) <= request.now.time_since_epoch().count())
      : load(c(&CardRow::due) <= request.now.time_since_epoch().count() &&
          in(&CardRow::vocabulary_id,
            select(&Vocabulary::id, where(c(&Vocabulary::language) == request.language))));
    std::vector<review::Card> result;
    for (const auto &row : rows) {
      review::Card card;
      card.word = impl_->db.get<Vocabulary>(row.vocabulary_id);
      if (row.stability.has_value() != row.difficulty.has_value() ||
        row.stability.has_value() != row.last_review.has_value())
        reject_review(ReviewStoreErrorKind::storage, "Incomplete stored FSRS memory.");
      if (row.stability) {
        card.memory = review::MemoryState{review::Days(static_cast<float>(*row.stability)),
          static_cast<float>(*row.difficulty)};
        card.last_review = std::chrono::sys_seconds(std::chrono::seconds(*row.last_review));
      }
      card.due = std::chrono::sys_seconds(std::chrono::seconds(row.due));
      card.revision = row.revision;
      result.push_back(std::move(card));
    }
    return result;
  });
}

std::expected<void, ReviewStoreError> Store::commit_review(const review::Commit &request) {
  return review_operation([&] {
    const auto &plan = request.plan;
    const auto rating = static_cast<unsigned>(request.rating);
    const auto match = static_cast<unsigned>(request.result);
    const auto now = plan.reviewed_at.time_since_epoch().count();
    if (rating < 1 || rating > 4 || match > static_cast<unsigned>(Match::incorrect) ||
      plan.vocabulary_id <= 0 || plan.revision < 0 ||
      plan.revision == std::numeric_limits<std::int64_t>::max() || now < 0 ||
      plan.elapsed.count() < 0 ||
      plan.elapsed.count() > std::numeric_limits<std::uint32_t>::max() ||
      !std::isfinite(plan.desired_retention) || plan.desired_retention <= 0 ||
      plan.desired_retention >= 1)
      reject_review(ReviewStoreErrorKind::invalid_input, "Invalid review submission.");
    const auto &outcome = plan.outcomes[rating - 1];
    const auto seconds = outcome.interval.count();
    if (!std::isfinite(outcome.memory.stability.count()) || outcome.memory.stability.count() <= 0 ||
      !std::isfinite(outcome.memory.difficulty) || outcome.memory.difficulty < 1 ||
      outcome.memory.difficulty > 10 || seconds <= 0 ||
      now > std::numeric_limits<std::int64_t>::max() - seconds ||
      outcome.due.time_since_epoch().count() != now + seconds)
      reject_review(ReviewStoreErrorKind::invalid_input, "Invalid FSRS outcome or due time.");
    impl_->db.transaction([&] {
      auto row = impl_->db.get_pointer<CardRow>(plan.vocabulary_id);
      if (!row)
        reject_review(ReviewStoreErrorKind::missing, "Review card no longer exists.");
      if (row->revision != plan.revision)
        reject_review(ReviewStoreErrorKind::stale, "Review card changed after it was loaded.");
      if (row->due > now || (row->last_review && *row->last_review > now))
        reject_review(ReviewStoreErrorKind::invalid_input,
          "Review card is not due or the clock moved backward.");
      const auto elapsed = row->last_review ? (now - *row->last_review) / 86400 : 0;
      if (elapsed != plan.elapsed.count())
        reject_review(ReviewStoreErrorKind::invalid_input,
          "Review elapsed time does not match its stored state.");
      impl_->db.insert(ReviewRow{0,
        plan.vocabulary_id,
        static_cast<int>(request.result),
        now,
        outcome.due.time_since_epoch().count()});
      // sqlite_orm's object insert returns int; the connection API preserves SQLite's 64-bit ID.
      const auto id = impl_->db.last_insert_rowid();
      execute_insert(impl_->db,
        insert(into<FsrsRow>(),
          columns(&FsrsRow::review_id,
            &FsrsRow::vocabulary_id,
            &FsrsRow::revision,
            &FsrsRow::rating,
            &FsrsRow::answer,
            &FsrsRow::elapsed_days,
            &FsrsRow::stability,
            &FsrsRow::difficulty,
            &FsrsRow::retention),
          values(std::make_tuple(id,
            plan.vocabulary_id,
            plan.revision + 1,
            int(rating),
            request.answer,
            plan.elapsed.count(),
            double(outcome.memory.stability.count()),
            double(outcome.memory.difficulty),
            double(plan.desired_retention)))));
      row->stability = outcome.memory.stability.count();
      row->difficulty = outcome.memory.difficulty;
      row->last_review = now;
      row->due = outcome.due.time_since_epoch().count();
      ++row->revision;
      impl_->db.update(*row);
      return true;
    });
  });
}

std::expected<std::vector<review::Record>, ReviewStoreError>
Store::review_history(std::int64_t vocabulary_id) {
  return review_operation([&] {
    if (vocabulary_id <= 0)
      reject_review(ReviewStoreErrorKind::invalid_input, "Invalid vocabulary ID.");
    const auto rows = impl_->db.get_all<FsrsRow>(where(c(&FsrsRow::vocabulary_id) == vocabulary_id),
      order_by(&FsrsRow::review_id));
    std::vector<review::Record> result;
    for (const auto &row : rows) {
      const auto event = impl_->db.get<ReviewRow>(row.review_id);
      result.push_back({row.review_id,
        row.vocabulary_id,
        row.revision,
        static_cast<review::Rating>(row.rating),
        static_cast<Match>(event.result),
        row.answer,
        std::chrono::sys_seconds(std::chrono::seconds(event.reviewed_at)),
        std::chrono::sys_seconds(std::chrono::seconds(event.next_review_at)),
        std::chrono::days(row.elapsed_days),
        {review::Days(static_cast<float>(row.stability)), static_cast<float>(row.difficulty)},
        static_cast<float>(row.retention)});
    }
    return result;
  });
}
} // namespace pawingo
