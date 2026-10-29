module;
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

module pawingo.review_ui;

import std;
import pawingo.ui;

namespace pawingo::ui {
namespace {
using config::ui_text;
enum class Phase { question, feedback, empty, unavailable };

struct State {
  Store &store;
  AnswerMatcher matcher;
  ReviewClock clock;
  std::optional<review::Scheduler> scheduler;
  std::deque<review::Card> queue;
  std::optional<review::Card> card;
  std::optional<review::Plan> plan;
  MatchResult assessment;
  std::string answer, status;
  Phase phase = Phase::empty;
  int selected = 0;
  float scroll = 0.F;
};

void advance(State &s) {
  s.answer.clear();
  s.status.clear();
  s.card.reset();
  s.plan.reset();
  s.scroll = 0.F;
  if (!s.scheduler) {
    auto scheduler = review::Scheduler::create();
    if (!scheduler) {
      s.phase = Phase::unavailable;
      s.status = scheduler.error().message;
      return;
    }
    s.scheduler.emplace(std::move(*scheduler));
  }
  if (s.queue.empty()) {
    auto queue = s.store.review_queue({s.clock(),
      static_cast<std::size_t>(config::application().integer("review.queue_batch_size", 1, 1000)),
      {}});
    if (!queue) {
      s.phase = Phase::unavailable;
      s.status = queue.error().message;
      return;
    }
    s.queue.assign(std::make_move_iterator(queue->begin()), std::make_move_iterator(queue->end()));
  }
  if (s.queue.empty()) {
    s.phase = Phase::empty;
    return;
  }
  s.card = std::move(s.queue.front());
  s.queue.pop_front();
  s.phase = Phase::question;
}

void check(State &s) {
  s.status.clear();
  auto plan = review::plan_review(*s.scheduler, *s.card, s.clock());
  if (!plan) {
    s.status = plan.error().message;
    return;
  }
  s.assessment = s.matcher(s.card->word, s.answer);
  s.plan = std::move(*plan);
  s.selected = s.assessment.result == Match::incorrect ? 0 : 2;
  s.status = s.assessment.feedback;
  s.scroll = 0.F;
  s.phase = Phase::feedback;
}

void grade(State &s, int selected) {
  auto result = s.store.commit_review(
    {*s.plan, static_cast<review::Rating>(selected + 1), s.assessment.result, s.answer}
  );
  if (!result) {
    if (result.error().kind == ReviewStoreErrorKind::stale ||
      result.error().kind == ReviewStoreErrorKind::missing) {
      s.queue.clear();
      advance(s);
      s.status = ui_text("review_storage_changed");
    } else
      s.status = result.error().message;
    return;
  }
  advance(s);
}

std::string interval(std::chrono::seconds value) {
  const auto seconds = value.count();
  if (seconds % 86400 == 0)
    return std::to_string(seconds / 86400) + "d";
  if (seconds % 3600 == 0)
    return std::to_string(seconds / 3600) + "h";
  if (seconds % 60 == 0)
    return std::to_string(seconds / 60) + "m";
  return std::to_string(seconds) + "s";
}
} // namespace

ReviewPanel review_panel(Store &store, AnswerMatcher matcher, ReviewClock clock) {
  using namespace ftxui;
  if (!matcher)
    matcher = [](const Vocabulary &word, const std::string &answer) {
      return review::match_meaning(word, answer);
    };
  if (!clock)
    clock = [] {
      return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    };
  auto state = std::make_shared<State>(store, std::move(matcher), std::move(clock));
  advance(*state);
  InputOption options;
  options.multiline = false;
  options.transform = [](InputState input) {
    return input.element | color(input.is_placeholder ? muted : ink);
  };
  auto input = text_input(&state->answer, ui_text("review_answer"), options);
  auto render = Renderer(input, [state, input] {
    const auto &defaults = config::application();
    if (!state->card) {
      return vbox({text(ui_text("review")) | color(ink) | hcenter,
               text(""),
               text(""),
               paragraph(state->phase == Phase::empty ? ui_text("review_no_due") : "") |
                 color(muted) | hcenter |
                 size(HEIGHT, EQUAL, defaults.integer("review.body_height", 4, 20)),
               text("") | size(HEIGHT, EQUAL, defaults.integer("review.source_height", 1, 3)),
               paragraph(state->status) | color(muted) |
                 size(HEIGHT, EQUAL, defaults.integer("review.status_height", 1, 5))}) |
        size(WIDTH, EQUAL, content_width);
    }
    const auto &word = state->card->word;
    Elements body;
    if (state->phase == Phase::question) {
      body = {text("→ " + word.meaning_language) | color(muted), text(""), input->Render()};
    } else {
      const auto match = state->assessment.result;
      body.push_back(text(ui_text(match == Match::incorrect ? "review_check_answer"
                         : match == Match::acceptable       ? "review_acceptable"
                                                            : "review_correct")) |
        color(match == Match::incorrect ? accent : olive));
      body.push_back(text(""));
      body.push_back(paragraph(word.meaning) | color(ink) |
        focusPositionRelative(0.F, state->scroll) | yframe |
        size(HEIGHT, EQUAL, defaults.integer("review.meaning_height", 1, 10)));
      body.push_back(paragraph(state->answer.empty() ? ui_text("review_blank_answer")
                                                     : state->answer) |
        color(secondary) | size(HEIGHT, EQUAL, 1));
      const auto labels = defaults.strings("review.rating_labels");
      if (labels.size() != 4)
        throw std::runtime_error("Review requires four rating labels.");
      Elements choices;
      for (int i = 0; i < 4; ++i) {
        choices.push_back(
          vbox({hbox({text(selection(state->selected == i)) | color(accent),
                  text(labels[i]) | color(state->selected == i ? ink : muted)}),
            text(interval(state->plan->outcomes[i].interval)) | color(muted) | hcenter}) |
          size(WIDTH, EQUAL, defaults.integer("review.rating_width", 8, 20))
        );
      }
      body.push_back(hbox(std::move(choices)) | hcenter);
    }
    return vbox({text(word.expression) | color(ink) | hcenter,
             text(word.reading) | color(secondary) | hcenter,
             text(""),
             vbox(std::move(body)) |
               size(HEIGHT, EQUAL, defaults.integer("review.body_height", 4, 20)),
             paragraph(word.source) | color(muted) |
               size(HEIGHT, EQUAL, defaults.integer("review.source_height", 1, 3)),
             paragraph(state->status) | color(muted) |
               size(HEIGHT, EQUAL, defaults.integer("review.status_height", 1, 5))}) |
      size(WIDTH, EQUAL, content_width);
  });
  auto component = CatchEvent(render, [state, input](Event event) {
    if (event == Event::Escape) {
      if (state->phase != Phase::feedback)
        return false;
      state->phase = Phase::question;
      state->plan.reset();
      state->status.clear();
      return true;
    }
    try {
      if (state->phase == Phase::question) {
        if (event == Event::Return) {
          check(*state);
          return true;
        }
        return input->OnEvent(event);
      }
      if (state->phase == Phase::feedback) {
        if (event == Event::ArrowLeft || event == Event::ArrowRight) {
          state->selected =
            std::clamp(state->selected + (event == Event::ArrowRight ? 1 : -1), 0, 3);
        } else if (event == Event::ArrowUp || event == Event::ArrowDown) {
          const auto step =
            config::application().integer("review.scroll_step_percent", 1, 100) / 100.F;
          state->scroll =
            std::clamp(state->scroll + (event == Event::ArrowDown ? step : -step), 0.F, 1.F);
        } else if (event == Event::Return)
          grade(*state, state->selected);
        else if (event.is_character() && event.character().size() == 1 &&
          event.character()[0] >= '1' && event.character()[0] <= '4')
          grade(*state, event.character()[0] - '1');
        return true;
      }
      if (event == Event::Return) {
        state->queue.clear();
        advance(*state);
      }
    } catch (const std::exception &e) {
      state->status = e.what();
    } catch (...) {
      state->status = ui_text("review_operation_failed");
    }
    return true;
  });
  return {component, [weak = std::weak_ptr<State>(state)] {
            const auto s = weak.lock();
            if (!s)
              return std::string{};
            return ui_text(s->phase == Phase::question ? "review_question_help"
                : s->phase == Phase::feedback          ? "review_feedback_help"
                                                       : "review_empty_help");
          }};
}
} // namespace pawingo::ui
