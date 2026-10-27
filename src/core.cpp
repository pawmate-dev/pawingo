#include "pawingo/core.hpp"
#include "pawingo_sql.hpp"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <sqlite_orm/sqlite_orm.h>
#include <stdexcept>
#include <utility>

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
      make_column("next_review_at", &ReviewRow::next_review_at)));
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
      if (sqlite3_exec(connection, sql::schema, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(connection);
        sqlite3_free(error);
        throw std::runtime_error(message);
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
  if (!row)
    return OnlinePolicy::ask;
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
struct Plugins::Impl {
  struct Loaded {
    void *handle;
    const PluginDescriptor *descriptor;
    LanguagePlugin *plugin;
  };
  std::vector<Loaded> loaded;
  std::vector<std::string> errors;
  ~Impl() {
    for (auto it = loaded.rbegin(); it != loaded.rend(); ++it) {
      // The destroy function and the object's vtable live in the shared library.
      it->descriptor->destroy(it->plugin);
      dlclose(it->handle);
    }
  }
};
Plugins::Plugins()
  : impl_(std::make_unique<Impl>()) {
}
Plugins::~Plugins() = default;
void Plugins::load(const std::string &path) {
  void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle)
    throw std::runtime_error(dlerror());
  try {
    auto entry = reinterpret_cast<PluginEntry>(dlsym(handle, plugin_entry_symbol));
    if (!entry)
      throw std::runtime_error("missing plugin entry point");
    const auto *d = entry();
    if (!d || d->api_version != plugin_api_version || !d->abi_tag ||
      std::strcmp(d->abi_tag, plugin_abi_tag) != 0 || !d->id || !*d->id || !d->name || !d->create ||
      !d->destroy)
      throw std::runtime_error("incompatible or invalid plugin descriptor");
    for (const auto &p : impl_->loaded)
      if (std::strcmp(p.descriptor->id, d->id) == 0)
        throw std::runtime_error("duplicate plugin id");
    auto *plugin = d->create();
    if (!plugin)
      throw std::runtime_error("plugin creation failed");
    try {
      impl_->loaded.push_back({handle, d, plugin});
    } catch (...) {
      d->destroy(plugin);
      throw;
    }
  } catch (...) {
    dlclose(handle);
    throw;
  }
}
void Plugins::discover(const std::string &directory) {
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) {
    if (ec)
      impl_->errors.push_back(directory + ": " + ec.message());
    return;
  }
  std::vector<std::filesystem::path> paths;
  for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end;
    it.increment(ec))
    if (it->path().extension() == ".so")
      paths.push_back(it->path());
  if (ec)
    impl_->errors.push_back(directory + ": " + ec.message());
  // Stable ordering makes duplicate-ID handling and the displayed panel order predictable.
  std::sort(paths.begin(), paths.end());
  for (const auto &p : paths)
    try {
      load(p.string());
    } catch (const std::exception &e) {
      impl_->errors.push_back(p.string() + ": " + e.what());
    }
}
std::vector<LanguagePlugin *> Plugins::languages() const {
  std::vector<LanguagePlugin *> out;
  for (auto &p : impl_->loaded)
    out.push_back(p.plugin);
  return out;
}
const std::vector<std::string> &Plugins::errors() const {
  return impl_->errors;
}
} // namespace pawingo
