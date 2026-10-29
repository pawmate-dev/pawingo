#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <sqlite3.h>

import std;
import pawingo.io;
import pawingo.ui;
import pawingo.review_ui;

using namespace pawingo;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

Vocabulary word() {
  return {"ja", "猫", "ねこ", "eng", "cat; feline", "fixture dictionary", ""};
}

void sql(const std::filesystem::path &path, const char *statement) {
  sqlite3 *db = nullptr;
  require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "open fault-injection database");
  const auto result = sqlite3_exec(db, statement, nullptr, nullptr, nullptr);
  const std::string error = sqlite3_errmsg(db);
  sqlite3_close(db);
  if (result != SQLITE_OK)
    throw std::runtime_error(error);
}

ftxui::Screen render(const ui::ReviewPanel &panel, int width = 80, int height = 24) {
  auto screen =
    ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
  ftxui::Render(screen,
    ftxui::vbox({panel.component->Render() | ftxui::center | ftxui::flex,
      ftxui::text(panel.help()) | ftxui::hcenter,
      ftxui::text("")}));
  return screen;
}

int main() {
  try {
    const auto now = std::chrono::sys_seconds(std::chrono::seconds(1000000));
    auto scheduler = review::Scheduler::create();
    require(bool(scheduler), "default scheduler");
    pawingo::io::TemporaryArtifact scratch(std::filesystem::temp_directory_path());
    const auto path = scratch.file();
    std::int64_t id = 0;
    {
      Store store(path.string());
      store.save(word());
      store.save(word());
      auto queue = store.review_queue({now, 10, {}});
      require(queue && queue->size() == 1 && !queue->front().memory && queue->front().revision == 0,
        "saving duplicate favorites enrolls one new card without fake memory");
      id = queue->front().word.id;
      auto plan = review::plan_review(*scheduler, queue->front(), now);
      require(bool(plan), "plan an enrolled card");
      // Force a failure after both log inserts to verify the complete transaction rolls back.
      sql(path,
        "CREATE TRIGGER fail_review BEFORE UPDATE ON review_cards BEGIN "
        "SELECT RAISE(ABORT, 'fixture rejects card update'); END;");
      auto failed = store.commit_review({*plan, review::Rating::good, Match::incorrect, "a cat"});
      require(!failed && failed.error().kind == ReviewStoreErrorKind::storage &&
          store.review_history(id)->empty() && store.review_timestamps(0, 2000000).empty() &&
          store.review_queue({now, 10, {}})->front().revision == 0,
        "failed memory update rolls back FSRS history, activity and card state together");
      sql(path,
        "DROP TRIGGER fail_review; INSERT INTO "
        "reviews(id,vocabulary_id,result,reviewed_at,next_review_at) "
        "VALUES(2147483648,1,0,1,2);");
      auto committed =
        store.commit_review({*plan, review::Rating::good, Match::incorrect, "a cat"});
      require(bool(committed), "retry the same pending answer after rolled-back storage failure");
      auto history = store.review_history(id);
      require(history && history->size() == 1 &&
          history->front().id > std::numeric_limits<int>::max() &&
          history->front().rating == review::Rating::good &&
          history->front().result == Match::incorrect && history->front().answer == "a cat" &&
          history->front().revision == 1,
        "recall rating is independent of answer matching and event IDs retain 64-bit precision");
      require(store.review_timestamps(1000000, 1000001).size() == 1,
        "one committed answer contributes exactly one activity event");
      auto repeated = store.commit_review({*plan, review::Rating::good, Match::correct, "cat"});
      require(!repeated && repeated.error().kind == ReviewStoreErrorKind::stale &&
          store.review_history(id)->size() == 1,
        "a repeated preview cannot grade the same revision twice");
      store.save(word());
      require(store.review_queue({now, 10, {}})->empty(),
        "resaving a favorite does not reset its due time");
      const auto due = plan->outcomes[2].due;
      auto resumed = store.review_queue({due, 10, {}});
      require(resumed && resumed->size() == 1 &&
          resumed->front().memory == plan->outcomes[2].memory &&
          resumed->front().last_review == now,
        "stored memory and completion time round-trip exactly");
      auto german = word();
      german.language = "de";
      german.expression = "Katze";
      german.reading.clear();
      store.save(german);
      auto filtered = store.review_queue({due, 10, "de"});
      require(filtered && filtered->size() == 1 && filtered->front().word.language == "de",
        "language filtering stays inside the bounded storage query");
      auto prioritized = store.review_queue({due, 10, {}});
      require(prioritized && prioritized->front().word.id == id,
        "due scheduled cards precede new favorites even when new due timestamps are zero");
      require(!store.review_queue({now, 0, {}}), "invalid queue bounds are rejected");
    }
    {
      Store reopened(path.string());
      require(reopened.review_history(id)->size() == 1 &&
          reopened.review_queue({now, 10, "ja"})->empty(),
        "restarting preserves FSRS history and future schedules");
    }
    // Simulate the pre-FSRS schema while retaining saved vocabulary and historical activity.
    sql(path, "DROP TABLE fsrs_reviews; DROP TABLE review_cards;");
    {
      Store migrated(path.string());
      auto queue = migrated.review_queue({now, 10, {}});
      require(queue && queue->size() == 2 && !queue->front().memory &&
          migrated.review_history(id)->empty() &&
          migrated.review_timestamps(0, 2000000).size() == 2,
        "legacy favorites enroll without inferred ratings, lost vocabulary or rewritten activity");
    }
    {
      Store store(":memory:");
      store.save({"de", "Katze", "", "eng", "cat", "German fixture", ""});
      store.save(word());
      auto german = word();
      german.meaning_language = "ger";
      german.meaning = "Katze";
      store.save(german);
      bool rejected = false;
      try {
        (void)ui::review_panel(store, "");
      } catch (const std::invalid_argument &) {
        rejected = true;
      }
      require(rejected, "review UI rejects an unscoped session");
      int matches = 0;
      auto panel = ui::review_panel(
        store,
        "ja",
        [&](const Vocabulary &entry, const std::string &answer) {
          require(entry.language == "ja", "matcher receives only its plugin language");
          ++matches;
          return review::match_meaning(entry, answer);
        },
        [&] {
          return now;
        }
      );
      for (const auto &answer : {"cat", "Katze"}) {
        require(render(panel).ToString().find("猫") != std::string::npos,
          "language scope uses source language rather than definition language");
        panel.component->OnEvent(ftxui::Event::Character(answer));
        panel.component->OnEvent(ftxui::Event::Return);
        panel.component->OnEvent(ftxui::Event::Character('3'));
      }
      panel.component->OnEvent(ftxui::Event::Return);
      require(matches == 2 && render(panel).ToString().find("No cards due") != std::string::npos &&
          store.review_history(1)->empty() && store.review_queue({now, 10, "de"})->size() == 1,
        "advance and refresh never include or grade another language");
    }
    {
      Store store(":memory:");
      store.save(word());
      auto clock = now;
      auto panel = ui::review_panel(store, "ja", {}, [&] {
        return clock;
      });
      auto question = render(panel);
      require(question.ToString().find("feline") == std::string::npos &&
          question.cursor().shape == ftxui::Screen::Cursor::BarBlinking,
        "question hides the answer and exposes an IME cursor before typing");
      require(store.review_history(1)->empty(), "opening the review panel records no activity");
      panel.component->OnEvent(ftxui::Event::Character("feline"));
      panel.component->OnEvent(ftxui::Event::Return);
      require(render(panel).ToString().find("cat; feline") != std::string::npos &&
          store.review_history(1)->empty(),
        "answer checking previews the solution without committing");
      require(panel.component->OnEvent(ftxui::Event::Escape) &&
          render(panel).ToString().find("feline") != std::string::npos &&
          store.review_history(1)->empty(),
        "Escape from feedback edits the draft instead of grading or leaving the session");
      panel.component->OnEvent(ftxui::Event::Return);
      panel.component->OnEvent(ftxui::Event::Character('4'));
      require(store.review_history(1)->front().rating == review::Rating::easy &&
          render(panel).ToString().find("No cards due") != std::string::npos,
        "explicit rating commits the selected outcome and advances the queue");
      panel.component->OnEvent(ftxui::Event::Return);
      require(store.review_history(1)->size() == 1 &&
          !panel.component->OnEvent(ftxui::Event::Escape),
        "refresh does not repeat a grade, and Escape delegates back from the completed queue");
    }
    {
      Store store(":memory:");
      store.save(word());
      auto clock = now;
      auto panel = ui::review_panel(store, "ja", {}, [&] {
        return clock;
      });
      panel.component->OnEvent(ftxui::Event::Character("dog"));
      panel.component->OnEvent(ftxui::Event::Return);
      panel.component->OnEvent(ftxui::Event::Return);
      auto history = store.review_history(1);
      require(history && history->front().rating == review::Rating::again &&
          history->front().due == now + std::chrono::minutes(10),
        "an unmatched answer defaults to Again with a persisted short relearning step");
      clock += std::chrono::seconds(599);
      panel.component->OnEvent(ftxui::Event::Return);
      require(render(panel).ToString().find("No cards due") != std::string::npos,
        "a relearning card stays outside the queue until its due time");
      clock += std::chrono::seconds(1);
      panel.component->OnEvent(ftxui::Event::Return);
      panel.component->OnEvent(ftxui::Event::Character("cat"));
      panel.component->OnEvent(ftxui::Event::Return);
      panel.component->OnEvent(ftxui::Event::Return);
      history = store.review_history(1);
      require(history && history->size() == 2 && history->back().elapsed == std::chrono::days(0) &&
          history->back().rating == review::Rating::good,
        "same-day relearning continues from stored memory instead of treating the card as new");
    }
    {
      Store store(":memory:");
      store.save(word());
      auto first = ui::review_panel(store, "ja", {}, [&] {
        return now;
      });
      auto second = ui::review_panel(store, "ja", {}, [&] {
        return now;
      });
      for (auto *panel : {&first, &second}) {
        panel->component->OnEvent(ftxui::Event::Character("cat"));
        panel->component->OnEvent(ftxui::Event::Return);
      }
      second.component->OnEvent(ftxui::Event::Return);
      first.component->OnEvent(ftxui::Event::Return);
      require(store.review_history(1)->size() == 1 &&
          render(first).ToString().find("changed elsewhere") != std::string::npos,
        "a stale session refreshes safely without duplicating a committed review");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
