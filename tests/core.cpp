#include <sqlite3.h>
#include <stdlib.h>
#include <unistd.h>

import std;
import pawingo.core;
import pawingo.plugin;
import pawingo.review;

class Provider : public pawingo::DictionaryProvider {
public:
  bool remote;
  int calls = 0;
  bool has_result;

  Provider(bool remote, bool has_result)
    : remote(remote),
      has_result(has_result) {
  }

  std::string id() const override {
    return remote ? "remote" : "local";
  }

  std::string name() const override {
    return id();
  }

  bool online() const override {
    return remote;
  }

  std::vector<pawingo::Vocabulary> search(const std::string &, const std::string &) override {
    ++calls;
    return has_result
      ? std::vector<pawingo::Vocabulary>{{"fixture", "word", "", "en", "meaning", id(), ""}}
      : std::vector<pawingo::Vocabulary>{};
  }
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main(int argc, char **argv) {
  try {
    require(argc == 2, "plugin path required");
    require(pawingo::review::exact("  hello  world ", "hello world"), "answer whitespace");
    require(!pawingo::review::exact("Hello", "hello"), "answer case preserved");
    require(pawingo::review::next_review(0, false, 0) == 600, "failed review interval");
    require(pawingo::review::next_review(0, true, 0) == 86400, "successful review interval");
    pawingo::Store store(":memory:");
    require(store.policy("service") == pawingo::OnlinePolicy::ask, "default policy");
    store.set_policy("service", pawingo::OnlinePolicy::allow);
    require(store.policy("service") == pawingo::OnlinePolicy::allow, "remembered policy");
    require(store.policy("other") == pawingo::OnlinePolicy::ask, "policy isolation");
    store.save({"fixture", "word", "reading", "en", "meaning", "dictionary", "context"});
    store.save({"fixture", "word", "reading", "en", "meaning", "dictionary", "context"});
    const auto words = store.vocabulary();
    require(words.size() == 1 && words[0].source == "dictionary" && words[0].id > 0,
      "saved vocabulary");
    store.record_review(words[0].id, pawingo::Match::correct, 100, 200);
    require(store.review_timestamps(100, 101).size() == 1 &&
        store.review_timestamps(101, 200).empty(),
      "review time range");
    bool foreign_key_rejected = false;
    try {
      store.record_review(-1, pawingo::Match::correct, 100, 200);
    } catch (const std::exception &) {
      foreign_key_rejected = true;
    }
    require(foreign_key_rejected, "review foreign key");
    bool rejected = false;
    try {
      store.save({"fixture", "word", "", "en", "meaning", "", ""});
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "missing source rejected");
    store.put_resource(
      {"dict", "Dictionary", pawingo::ResourceKind::dictionary, "fixture", "/tmp/dict", true, 10}
    );
    require(store.resources(pawingo::ResourceKind::dictionary, "fixture").size() == 1,
      "resource lookup");
    store.put_resource(
      {"dict", "Updated", pawingo::ResourceKind::dictionary, "fixture", "/tmp/new", false, 5}
    );
    auto updated = store.resources(pawingo::ResourceKind::dictionary, "fixture");
    require(updated.size() == 1 && updated[0].name == "Updated" && !updated[0].enabled,
      "resource upsert");
    char file[] = "/tmp/pawingo-legacy-XXXXXX";
    int fd = mkstemp(file);
    require(fd >= 0, "create legacy database");
    close(fd);
    sqlite3 *legacy = nullptr;
    require(sqlite3_open(file, &legacy) == SQLITE_OK, "open legacy database");
    int rc = sqlite3_exec(legacy,
      "CREATE TABLE vocabulary (id INTEGER PRIMARY KEY, language TEXT NOT NULL, expression TEXT "
      "NOT NULL, reading TEXT NOT NULL, meaning_language TEXT NOT NULL, meaning TEXT NOT NULL, "
      "source TEXT NOT NULL, context TEXT NOT NULL, "
      "UNIQUE(language,expression,reading,meaning_language,meaning,source,context)); INSERT INTO "
      "vocabulary VALUES (42,'ja','word','reading','en','meaning','legacy','context');",
      nullptr,
      nullptr,
      nullptr);
    sqlite3_close(legacy);
    require(rc == SQLITE_OK, "create legacy data");
    {
      pawingo::Store existing(file);
      auto old = existing.vocabulary();
      require(old.size() == 1 && old[0].id == 42 && old[0].source == "legacy",
        "legacy data readable");
      existing.save(old[0]);
      require(existing.vocabulary().size() == 1, "legacy duplicate ignored");
    }
    unlink(file);
    Provider local(false, true), remote(true, true);
    int prompts = 0;
    auto ask = [&](const pawingo::DictionaryProvider &, const std::string &) {
      ++prompts;
      return pawingo::Consent::always;
    };
    auto found = pawingo::search_dictionaries(store, {&remote, &local}, "word", "en", ask);
    require(found.size() == 1 && found[0].source == "local" && remote.calls == 0 && prompts == 0,
      "local priority");
    local.has_result = false;
    found = pawingo::search_dictionaries(store, {&local, &remote}, "word", "en", ask);
    require(found.size() == 1 && found[0].source == "remote" && prompts == 1, "online consent");
    found = pawingo::search_dictionaries(store, {&local, &remote}, "word", "en", ask);
    require(prompts == 1 && remote.calls == 2, "online consent remembered");
    store.set_policy("remote", pawingo::OnlinePolicy::offline);
    found = pawingo::search_dictionaries(store, {&local, &remote}, "word", "en", ask);
    require(found.empty() && remote.calls == 2, "offline policy");
    pawingo::Plugins plugins;
    plugins.load(argv[1]);
    require(plugins.languages().size() == 1, "plugin loaded");
    auto *plugin = plugins.languages()[0];
    require(plugin->language() == "fixture" && plugin->panels().size() == 1, "plugin capabilities");
    {
      auto panel = plugin->create_panel("example", store);
      require(bool(panel), "plugin panel");
    }
    rejected = false;
    try {
      plugins.load(argv[1]);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "duplicate plugin rejected");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
