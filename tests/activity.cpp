#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

import std;
import pawingo.learning;
import pawingo.ui;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main() {
  using namespace pawingo;
  try {
    Store store(":memory:");
    store.save({"ja", "猫", "ねこ", "eng", "cat", "dictionary A", ""});
    store.save({"ja", "猫", "ねこ", "ger", "Katze", "dictionary A", ""});
    store.save({"ja", "猫", "ねこ", "eng", "cat", "dictionary B", ""});
    store.save({"de", "Katze", "", "eng", "cat", "dictionary A", ""});
    store.save({"ja", "水", "みず", "eng", "water", "dictionary A", ""});
    store.save({"ja", "水", "すい", "eng", "Wednesday", "dictionary A", ""});
    const std::array kinds{review::QuestionType::meaning,
      review::QuestionType::reading,
      review::QuestionType::listening};
    require(bool(store.enroll_review_cards(1, kinds)) && store.learning_activity(0, 100000).empty(),
      "saved words and unreviewed question enrollment do not manufacture activity");
    auto scheduler = review::Scheduler::create();
    const auto now = std::chrono::sys_seconds(std::chrono::seconds(10010));
    auto queue = store.review_queue({now, 100, "ja"});
    for (const auto &card : *queue) {
      if (card.word.id != 1)
        continue;
      auto plan = review::plan_review(*scheduler, card, now);
      require(plan &&
          bool(store.commit_review({*plan, review::Rating::again, Match::incorrect, "?"})),
        "all question types produce committed study activity even when recall fails");
    }
    for (int i = 0; i < 20; ++i)
      store.record_review(1, Match::correct, 10000, 20000);
    for (int id = 2; id <= 6; ++id)
      store.record_review(id, Match::correct, 10000 + id * 10, 20000);
    store.record_review(1, Match::correct, 90000, 100000);
    require(store.learning_activity(10000, 10001).size() == 20 &&
        store.learning_activity(10001, 10010).empty(),
      "activity queries use half-open timestamp bounds");
    auto events = store.learning_activity(0, 100000);
    require(events.size() == 29 && events.front().language == "ja" &&
        events.front().expression == "猫" && events.front().reading == "ねこ" &&
        events.front().reviewed_at <= events.back().reviewed_at,
      "activity query joins real vocabulary identity and retains chronological legacy/FSRS events");
    auto counts = learning_word_counts(events, [](std::int64_t stamp) {
      return std::to_string(stamp / 86400);
    });
    require(counts.size() == 2 && counts.at("0") == 4 && counts.at("1") == 1,
      "same-day repetitions, question types, definition languages and sources count one word; "
      "distinct languages/readings and days remain separate");
    const std::array boundary{LearningActivity{86399, "ja", "猫", "ねこ"},
      LearningActivity{86400, "ja", "猫", "ねこ"}};
    auto utc = learning_word_counts(boundary, [](std::int64_t stamp) {
      return std::to_string(stamp / 86400);
    });
    auto local = learning_word_counts(boundary, [](std::int64_t stamp) {
      return std::to_string((stamp + 3600) / 86400);
    });
    require(utc.size() == 2 && local.size() == 1 && local.at("1") == 1,
      "calendar date policy is supplied by the frontend and deduplicates after timezone grouping");
    require(learning_word_counts(
              {},
              [](std::int64_t) {
                return "today";
              }
            ).empty(),
      "empty histories stay empty");
    const std::array<std::size_t, 9> words{0, 1, 4, 5, 9, 10, 19, 20, 1000};
    const std::array<std::size_t, 9> levels{0, 1, 1, 2, 2, 3, 3, 4, 4};
    for (std::size_t i = 0; i < words.size(); ++i)
      require(ui::activity_level(words[i]) == levels[i],
        "heat thresholds reflect distinct learned-word quantities");
    auto dot = [](std::size_t words) {
      auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
      ftxui::Render(screen, ui::activity_dot(words));
      return screen.PixelAt(0, 0);
    };
    std::vector<ftxui::Color> colors;
    const std::array original_colors{ui::faint, ui::muted, ui::secondary, ui::olive, ui::ink};
    const std::array quantities{0U, 1U, 5U, 10U, 20U};
    const std::array marks{"·", "◦", "○", "●", "●"};
    for (std::size_t i = 0; i < quantities.size(); ++i) {
      const auto regular = dot(quantities[i]);
      require(regular.foreground_color == original_colors[i] && regular.character == marks[i],
        "all dates use count-based marks and the original warm gray/olive/ivory palette");
      require(std::ranges::none_of(colors,
                [&](const auto &color) {
                  return color == regular.foreground_color;
                }),
        "every heat level has a distinct foreground shade");
      colors.push_back(regular.foreground_color);
      require(!regular.inverted,
        "heat dots preserve the terminal background without inverse blocks");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
