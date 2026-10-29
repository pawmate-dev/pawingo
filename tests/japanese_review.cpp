#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <sqlite3.h>

import std;
import pawingo.japanese.review;
import pawingo.io;
import pawingo.ui;

using namespace pawingo;
using namespace pawingo::japanese;
using enum review::QuestionType;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

Vocabulary cat() {
  return {"ja", "猫", "ねこ", "eng", "cat; feline", "fixture", ""};
}

void execute(const std::filesystem::path &path, const std::string &sql) {
  sqlite3 *db = nullptr;
  require(sqlite3_open(path.string().c_str(), &db) == SQLITE_OK, "open legacy database");
  char *error = nullptr;
  const auto code = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  const std::string message = error ? error : "";
  sqlite3_free(error);
  sqlite3_close(db);
  if (code != SQLITE_OK)
    throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path);
  require(input.good(), "open schema fixture");
  return {std::istreambuf_iterator<char>(input), {}};
}

ftxui::Screen screen(const ui::ReviewPanel &panel) {
  auto result = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
  ftxui::Render(result, panel.component->Render());
  return result;
}

template <typename F> void await(F predicate) {
  for (int i = 0; i < 2000; ++i) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("speech did not reach the expected completion boundary");
}

int main(int argc, char **argv) {
  using namespace ftxui;
  try {
    require(argc == 2, "project root required for legacy SQL fixtures");
    const auto now = std::chrono::sys_seconds(std::chrono::seconds(1000000));
    require(review_answer_key(" ｶﾞｯｺｳ　", true) == "がっこう" &&
        review_answer_key("がっこう", true) == "がっこう" &&
        review_answer_key("ﾊﾟﾋﾟﾌﾟﾍﾟﾎﾟ", true) == "ぱぴぷぺぽ" &&
        review_answer_key("ヴ", true) == "ゔ",
      "width, scripts and voicing normalize without losing small kana");
    review::Card card;
    card.word = cat();
    card.kind = reading;
    require(match_review(card, "ネコ").result == Match::acceptable &&
        match_review(card, "ねこ").result == Match::correct &&
        match_review(card, "neko").result == Match::incorrect,
      "reading accepts kana equivalents, not romaji guesses");
    for (auto [expected, answer] : {std::pair{"きって", "きて"},
           {"おばあさん", "おばさん"},
           {"きゃく", "きやく"},
           {"コーヒー", "コヒ"}}) {
      card.word.reading = expected;
      require(match_review(card, answer).result == Match::incorrect,
        "sokuon, long vowels and small kana remain meaningful errors");
    }
    card.word = cat();
    card.kind = listening;
    require(match_review(card, "猫").result == Match::correct &&
        match_review(card, "ねこ").result == Match::incorrect,
      "dictation requires the saved kanji spelling");
    require(review_types(cat()).size() == 3,
      "kanji with a kana reading supports all three question types");
    auto kana = cat();
    kana.expression = "ねこ";
    require(review_types(kana) == std::vector{meaning},
      "kana-only words do not create trivial kanji questions");
    kana = cat();
    kana.reading.clear();
    require(review_types(kana) == std::vector{meaning},
      "missing readings do not create unanswerable cards");
    require(review_answer_key(std::string("\xFF"), true).empty(),
      "malformed UTF-8 cannot match an answer");

    io::TemporaryArtifact directory(std::filesystem::temp_directory_path());
    const auto dbfile = directory.file();
    execute(dbfile,
      read(std::filesystem::path(argv[1]) / "sql/schema.sql") +
        read(std::filesystem::path(argv[1]) / "sql/review.sql") +
        "INSERT INTO vocabulary VALUES(7,'ja','猫','ねこ','eng','cat; feline','fixture','');"
        "INSERT INTO review_cards VALUES(7,2.5,6,10,20,3);"
        "INSERT INTO reviews VALUES(17,7,0,10,20);"
        "INSERT INTO fsrs_reviews VALUES(17,7,3,3,'cat',0,2.5,6,0.9,'fsrs-6');");
    {
      Store migrated(dbfile.string());
      auto queue = migrated.review_queue({now, 10, "ja"});
      require(queue && queue->size() == 1 && queue->front().kind == meaning &&
          queue->front().revision == 3 && queue->front().memory->stability.count() == 2.5F &&
          migrated.review_history(7)->front().id == 17,
        "legacy migration preserves IDs, meaning memory, revisions, due times and history");
      auto types = review_types(queue->front().word);
      require(bool(migrated.enroll_review_cards(7, types)),
        "enroll independent new question types");
      queue = migrated.review_queue({now, 10, "ja"});
      require(queue->size() == 3 && queue->at(1).kind == reading && !queue->at(1).memory &&
          queue->at(2).kind == listening && !queue->at(2).memory,
        "new reading and listening memories start empty rather than copy meaning progress");
      auto scheduler = review::Scheduler::create();
      auto plan = review::plan_review(*scheduler, queue->at(1), now);
      require(plan && plan->kind == reading &&
          bool(migrated.commit_review({*plan, review::Rating::good, Match::correct, "ねこ"})),
        "grading a reading card preserves its question identity");
      require(!migrated.commit_review({*plan, review::Rating::good, Match::correct, "ねこ"}),
        "duplicate submission cannot grade the same typed card twice");
      auto history = migrated.review_history(7);
      require(history->size() == 2 && history->back().kind == reading &&
          history->front().kind == meaning,
        "history distinguishes question types with independent revision counters");
      migrated.save(cat());
    }
    {
      Store reopened(dbfile.string());
      const auto queue = reopened.review_queue({now, 10, "ja"});
      require(queue->size() == 2 && queue->front().kind == meaning &&
          queue->front().revision == 3 && queue->back().kind == listening &&
          reopened.review_history(7)->size() == 2,
        "reopening and resaving never reset another question's schedule");
    }
    {
      Store store(":memory:");
      store.save(cat());
      auto content = review_content({});
      std::atomic<int> calls = 0;
      std::atomic<bool> release = false, fail = true, cancelled = false;
      content.speak = [&](const Vocabulary &word,
                        std::stop_token stop) -> std::expected<void, review::Error> {
        require(word.expression == "猫" && word.reading == "ねこ",
          "speech receives spelling and reading context");
        ++calls;
        while (!release && !stop.stop_requested())
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancelled = stop.stop_requested();
        if (fail)
          return std::unexpected(review::Error{review::ErrorKind::configuration,
            "offline voice missing"});
        return {};
      };
      auto panel = ui::review_panel(
        store,
        "ja",
        {},
        [&] {
          return now;
        },
        content
      );
      require(store.review_queue({now, 10, "ja"})->size() == 3,
        "plugin enrollment creates three cards without activity");
      panel.component->OnEvent(Event::Character("cat"));
      panel.component->OnEvent(Event::Return);
      panel.component->OnEvent(Event::Character('3'));
      auto question = screen(panel).ToString();
      require(question.find("猫") != std::string::npos &&
          question.find("ねこ") == std::string::npos,
        "reading question hides its answer before checking");
      panel.component->OnEvent(Event::CtrlP);
      require(calls == 0, "speech cannot reveal a reading answer before checking");
      panel.component->OnEvent(Event::Character("ねご"));
      panel.component->OnEvent(Event::Return);
      auto feedback = screen(panel);
      bool wrong = false, correct = false;
      for (int y = 0; y < feedback.dimy(); ++y)
        for (int x = 0; x < feedback.dimx(); ++x) {
          const auto &pixel = feedback.PixelAt(x, y);
          wrong |= pixel.character == "ご" && pixel.foreground_color == ui::accent;
          correct |= pixel.character == "こ" && pixel.foreground_color == ui::olive;
        }
      require(wrong && correct, "feedback visually distinguishes wrong and expected kana");
      panel.component->OnEvent(Event::Character('2'));
      question = screen(panel).ToString();
      require(question.find("猫") == std::string::npos &&
          question.find("ねこ") == std::string::npos &&
          question.find("cat; feline") != std::string::npos,
        "listening hides spelling, reading and provenance while meanings disambiguate homophones");
      panel.component->OnEvent(Event::Character("猫"));
      panel.component->OnEvent(Event::Return);
      require(store.review_history(1)->size() == 2 &&
          panel.help().find("listen") != std::string::npos,
        "an unheard listening question cannot be assessed or graded");
      panel.component->OnEvent(Event::CtrlP);
      await([&] {
        return calls.load() == 1;
      });
      const auto start = std::chrono::steady_clock::now();
      panel.component->OnEvent(Event::ArrowLeft);
      panel.component->OnEvent(Event::CtrlP);
      require(calls == 1 &&
          std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100),
        "speech stays asynchronous and rejects overlapping playback");
      release = true;
      await([&] {
        return screen(panel).ToString().find("offline voice missing") != std::string::npos;
      });
      panel.component->OnEvent(Event::Return);
      require(store.review_history(1)->size() == 2 &&
          panel.help().find("listen") != std::string::npos,
        "failed audio leaves listening ungraded and retryable");
      fail = false;
      await([&] {
        panel.component->OnEvent(Event::CtrlP);
        return calls.load() >= 2;
      });
      await([&] {
        return screen(panel).ToString().find("Played.") != std::string::npos;
      });
      panel.component->OnEvent(Event::Return);
      panel.component->OnEvent(Event::Character('4'));
      const auto history = store.review_history(1);
      require(history->size() == 3 && history->at(0).kind == meaning &&
          history->at(1).kind == reading && history->at(2).kind == listening &&
          history->at(1).rating == review::Rating::hard &&
          history->at(2).rating == review::Rating::easy &&
          screen(panel).ToString().find("No cards due") != std::string::npos,
        "each question commits its own rating and memory and completes without duplicate activity");
      // A reopened listening card's cancellation must finish before captured services are released.
      Store another(":memory:");
      another.save(cat());
      auto only_listening = content;
      only_listening.types = [](const Vocabulary &) {
        return std::vector{listening};
      };
      // Put the default meaning card into the future so the listening card is first.
      auto scheduler = review::Scheduler::create();
      auto plan =
        review::plan_review(*scheduler, another.review_queue({now, 1, "ja"})->front(), now);
      require(bool(another.commit_review({*plan, review::Rating::good, Match::correct, "cat"})),
        "prepare listening-only due queue");
      release = false;
      cancelled = false;
      auto pending = ui::review_panel(
        another,
        "ja",
        {},
        [&] {
          return now;
        },
        only_listening
      );
      const int previous = calls.load();
      pending.component->OnEvent(Event::CtrlP);
      await([&] {
        return calls.load() > previous;
      });
      require(!pending.component->OnEvent(Event::Escape),
        "Escape delegates back and cancels listening");
      pending.component.reset();
      require(cancelled, "panel destruction joins its cancelled speech worker");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
