module;
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

module pawingo.review_ui;

import std;
import pawingo.ui;
import pawingo.tasks;

namespace pawingo::ui {
namespace {
using config::ui_text;
enum class Phase { question, feedback, empty, unavailable };

struct State {
  Store &store;
  std::string language;
  AnswerMatcher matcher;
  ReviewClock clock;
  ReviewContent content;
  ReviewQuestion question;
  std::optional<review::Scheduler> scheduler;
  std::deque<review::Card> queue;
  std::optional<review::Card> card;
  std::optional<review::Plan> plan;
  MatchResult assessment;
  std::string answer, status;
  Phase phase = Phase::empty;
  int selected = 0;
  float scroll = 0.F;
  std::mutex audio_mutex;
  std::string audio_status;
  bool heard = false;
  std::uint64_t generation = 0;
  // Declared last so callbacks finish before any state they capture is destroyed.
  tasks::BackgroundJob speech_job;
};

std::pair<bool, std::string> audio_state(State &s) {
  std::lock_guard lock(s.audio_mutex);
  return {s.heard, s.audio_status};
}

void play(State &s) {
  if (!s.card || !s.content.speak || s.speech_job.running())
    return;
  const auto word = s.card->word;
  std::uint64_t generation;
  {
    std::lock_guard lock(s.audio_mutex);
    generation = s.generation;
    s.audio_status = ui_text("review_playing");
  }
  s.speech_job.start(
    [&s, word, generation](std::stop_token stop) {
      auto result = s.content.speak(word, stop);
      std::lock_guard lock(s.audio_mutex);
      if (generation == s.generation && !stop.stop_requested()) {
        if (result)
          s.heard = true;
        s.audio_status = result ? ui_text("review_played") : result.error().message;
      }
    },
    [&s, generation](std::exception_ptr failure) {
      if (failure) {
        std::lock_guard lock(s.audio_mutex);
        if (generation == s.generation)
          s.audio_status = ui_text("review_audio_failed");
      }
      if (s.content.redraw)
        s.content.redraw();
    }
  );
}

void advance(State &s) {
  s.speech_job.request_stop();
  {
    std::lock_guard lock(s.audio_mutex);
    ++s.generation;
    s.heard = false;
    s.audio_status.clear();
  }
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
      s.language});
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
  s.question = s.content.question ? s.content.question(*s.card)
                                  : ReviewQuestion{s.card->word.expression,
                                      s.card->word.reading,
                                      s.card->word.meaning,
                                      ui_text("review_answer")};
  s.phase = Phase::question;
}

void check(State &s) {
  s.status.clear();
  if (s.question.audio_only && !audio_state(s).first) {
    s.status = ui_text("review_listen_first");
    return;
  }
  auto plan = review::plan_review(*s.scheduler, *s.card, s.clock());
  if (!plan) {
    s.status = plan.error().message;
    return;
  }
  s.assessment =
    s.content.match ? s.content.match(*s.card, s.answer) : s.matcher(s.card->word, s.answer);
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

ReviewPanel review_panel(Store &store,
  const std::string &language,
  AnswerMatcher matcher,
  ReviewClock clock,
  ReviewContent content) {
  if (language.find_first_not_of(" \t\r\n") == std::string::npos)
    throw std::invalid_argument("A review session requires a language code.");
  using namespace ftxui;
  if (!matcher)
    matcher = [](const Vocabulary &word, const std::string &answer) {
      return review::match_meaning(word, answer);
    };
  if (!clock)
    clock = [] {
      return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    };
  auto state = std::make_shared<State>(store,
    language,
    std::move(matcher),
    std::move(clock),
    std::move(content));
  if (state->content.types) {
    for (const auto &word : store.vocabulary()) {
      if (word.language != language)
        continue;
      auto types = state->content.types(word);
      auto enrolled = store.enroll_review_cards(word.id, types);
      if (!enrolled)
        throw std::runtime_error(enrolled.error().message);
    }
  }
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
    const auto &question = state->question;
    Elements body;
    if (state->phase == Phase::question) {
      body = {text(question.placeholder + "  → " + word.meaning_language) | color(muted),
        text(""),
        input->Render()};
      if (question.audio_only)
        body.push_back(text(ui_text("review_audio_prompt")) | color(secondary));
    } else {
      const auto match = state->assessment.result;
      body.push_back(text(ui_text(match == Match::incorrect ? "review_check_answer"
                         : match == Match::acceptable       ? "review_acceptable"
                                                            : "review_correct")) |
        color(match == Match::incorrect ? accent : olive));
      body.push_back(text(""));
      const auto difference = review::answer_difference(state->answer, question.expected);
      auto highlighted = [&](const std::vector<review::AnswerSpan> &spans, bool expected) {
        Elements pieces;
        for (const auto &span : spans) {
          const auto shade = span.changed && match == Match::incorrect
            ? (expected ? olive : accent)
            : (expected ? ink : secondary);
          // Scalar-sized inline nodes let long unbroken Japanese answers wrap inside the viewport.
          for (std::size_t i = 0; i < span.text.size();) {
            const auto start = i++;
            while (
              i < span.text.size() && (static_cast<unsigned char>(span.text[i]) & 0xC0) == 0x80)
              ++i;
            pieces.push_back(text(span.text.substr(start, i - start)) | color(shade));
          }
        }
        if (pieces.empty())
          pieces.push_back(text(ui_text("review_blank_answer")) | color(muted));
        return hflow(std::move(pieces));
      };
      Elements solution{highlighted(difference.second, true)};
      if (state->card->kind != review::QuestionType::meaning)
        solution.push_back(paragraph(word.meaning) | color(secondary));
      body.push_back(vbox(std::move(solution)) | focusPositionRelative(0.F, state->scroll) |
        yframe | size(HEIGHT, EQUAL, defaults.integer("review.meaning_height", 1, 10)));
      body.push_back(highlighted(difference.first, false) | size(HEIGHT, EQUAL, 1));
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
    const bool feedback = state->phase == Phase::feedback;
    const auto audio = audio_state(*state);
    std::string status = state->status;
    if (!audio.second.empty())
      status += (status.empty() ? "" : " · ") + audio.second;
    return vbox({paragraph(feedback ? word.expression : question.title) | color(ink) | hcenter |
               size(HEIGHT, EQUAL, 1),
             paragraph(feedback ? word.reading : question.subtitle) | color(secondary) | hcenter |
               size(HEIGHT, EQUAL, defaults.integer("review.subtitle_height", 1, 3)),
             text(""),
             vbox(std::move(body)) |
               size(HEIGHT, EQUAL, defaults.integer("review.body_height", 4, 20)),
             paragraph(question.audio_only && !feedback ? "" : word.source) | color(muted) |
               size(HEIGHT, EQUAL, defaults.integer("review.source_height", 1, 3)),
             paragraph(status) | color(muted) |
               size(HEIGHT, EQUAL, defaults.integer("review.status_height", 1, 5))}) |
      size(WIDTH, EQUAL, content_width);
  });
  auto component = CatchEvent(render, [state, input](Event event) {
    if (event == Event::Escape) {
      if (state->phase != Phase::feedback) {
        state->speech_job.request_stop();
        return false;
      }
      state->phase = Phase::question;
      state->plan.reset();
      state->status.clear();
      return true;
    }
    try {
      if (event == Event::CtrlP && state->card) {
        // Reading answers must not be revealed by speech before the learner checks them.
        if (state->phase == Phase::feedback || state->card->kind != review::QuestionType::reading)
          play(*state);
        return true;
      }
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
            if (s->phase == Phase::question && s->question.audio_only)
              return ui_text("review_listening_help");
            auto help = ui_text(s->phase == Phase::question ? "review_question_help"
                : s->phase == Phase::feedback               ? "review_feedback_help"
                                                            : "review_empty_help");
            if (s->content.speak && s->card &&
              (s->phase == Phase::feedback || s->card->kind == review::QuestionType::meaning))
              return ui_text("review_speak_hint") + help;
            return help;
          }};
}
} // namespace pawingo::ui
