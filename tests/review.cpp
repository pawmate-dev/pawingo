import std;
import pawingo.review;

using namespace pawingo::review;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool near(float a, float b) {
  return std::abs(a - b) < 0.0001F;
}

int main() {
  try {
    static_assert(!std::is_copy_constructible_v<Scheduler>);
    static_assert(std::is_nothrow_move_constructible_v<Scheduler>);
    auto model = Scheduler::create();
    require(bool(model), "create default FSRS model");
    pawingo::Vocabulary vocabulary{"ja", "猫", "ねこ", "eng", "cat; feline", "fixture", "", 1};
    require(match_meaning(vocabulary, "feline").result == pawingo::Match::correct &&
        match_meaning(vocabulary, " CAT ").result == pawingo::Match::acceptable,
      "a saved gloss alternative and conservative normalized variant can answer a meaning card");
    require(match_meaning(vocabulary, "ca").result == pawingo::Match::incorrect &&
        match_meaning(vocabulary, "a cat").result == pawingo::Match::incorrect &&
        match_meaning(vocabulary, " ").result == pawingo::Match::incorrect,
      "meaning matching does not accept substrings, guessed synonyms or blank answers");
    auto next = model->next_states({});
    require(bool(next), "schedule new card");
    // Golden values from the pinned upstream 6.6.2 scheduling example, not product YAML.
    require(near(next->again.memory.stability.count(), 0.212F) &&
        near(next->hard.memory.stability.count(), 1.2931F) &&
        near(next->good.memory.stability.count(), 2.3065F) &&
        near(next->easy.memory.stability.count(), 8.2956F),
      "all four rating states preserve upstream values through the C ABI");
    require(near(next->good.interval.count(), 2.3065F) &&
        near(next->good.memory.difficulty, 2.118104F),
      "default retention preserves fractional-day intervals and difficulty");
    const auto now = std::chrono::sys_seconds(std::chrono::seconds(1000000));
    Card card{vocabulary, {}, {}, {}, 0};
    auto planned = plan_review(*model, card, now);
    require(planned && planned->outcomes[0].interval == std::chrono::minutes(10) &&
        planned->outcomes[2].interval == std::chrono::days(2) &&
        planned->outcomes[3].interval == std::chrono::days(8) &&
        planned->outcomes[0].memory == next->again.memory,
      "host policy rounds daily intervals and applies a short Again step without replacing FSRS "
      "memory");
    card.memory = planned->outcomes[2].memory;
    card.last_review = now;
    card.due = planned->outcomes[2].due;
    card.revision = 1;
    auto future = plan_review(*model, card, now);
    require(!future && future.error().kind == ErrorKind::invalid_input,
      "a future-due card cannot be planned early");
    auto due = plan_review(*model, card, card.due);
    require(due && due->elapsed == std::chrono::days(2),
      "persisted memory resumes with elapsed whole 24-hour days");
    Card inconsistent_card{vocabulary, next->good.memory, {}, {}, 0};
    require(!plan_review(*model, inconsistent_card, now),
      "memory requires a corresponding prior review time");
    Card overflow_card{vocabulary, {}, {}, {}, 0};
    auto time_overflow = plan_review(*model,
      overflow_card,
      std::chrono::sys_seconds(std::chrono::seconds(std::numeric_limits<std::int64_t>::max())));
    require(!time_overflow && time_overflow.error().kind == ErrorKind::invalid_input,
      "interval scheduling cannot overflow Unix-second timestamps");

    auto strict = model->next_states({.desired_retention = 0.95F});
    require(strict && strict->good.interval < next->good.interval &&
        strict->good.memory == next->good.memory,
      "higher desired retention shortens the interval without changing learned memory");
    auto later = model->next_states({.memory = next->good.memory, .elapsed = std::chrono::days(5)});
    require(later && later->good.memory.stability > next->good.memory.stability,
      "successful delayed recall increases memory stability");
    require(later->again.memory.stability < later->good.memory.stability,
      "forgotten and recalled outcomes remain distinct");
    auto same_day =
      model->next_states({.memory = next->good.memory, .elapsed = std::chrono::days(0)});
    require(same_day && same_day->good.interval.count() > 0, "same-day review is supported");

    const std::array<Review, 2> history{
      {{Rating::good, std::chrono::days(0)}, {Rating::easy, std::chrono::days(5)}}};
    auto replay = model->memory_state(history);
    require(replay && *replay == later->easy.memory,
      "history reconstruction agrees with incremental scheduling");
    auto continued = model->memory_state(std::span(history).subspan(1), next->good.memory);
    require(continued && *continued == *replay,
      "partial history after a persisted state reconstructs the same memory");

    for (const float retention : {0.F,
           1.F,
           -0.1F,
           std::numeric_limits<float>::quiet_NaN(),
           std::numeric_limits<float>::infinity()}) {
      auto invalid = model->next_states({.desired_retention = retention});
      require(!invalid && invalid.error().kind == ErrorKind::invalid_input,
        "invalid retention returns a typed error");
    }
    auto negative =
      model->next_states({.memory = next->good.memory, .elapsed = std::chrono::days(-1)});
    require(!negative && negative.error().kind == ErrorKind::invalid_input,
      "negative elapsed days are rejected before unsigned conversion");
    auto overflow = model->next_states({.memory = next->good.memory,
      .elapsed = std::chrono::days(std::int64_t(std::numeric_limits<std::uint32_t>::max()) + 1)});
    require(!overflow && overflow.error().kind == ErrorKind::invalid_input,
      "elapsed days cannot truncate at the ABI boundary");
    auto inconsistent = model->next_states({.elapsed = std::chrono::days(1)});
    require(!inconsistent && inconsistent.error().kind == ErrorKind::invalid_input,
      "a new card cannot have an elapsed previous-review interval");
    for (auto memory : {MemoryState{Days(0.F), 5.F},
           MemoryState{Days(1.F), 11.F},
           MemoryState{Days(std::numeric_limits<float>::infinity()), 5.F}}) {
      auto invalid = model->next_states({.memory = memory});
      require(!invalid && invalid.error().kind == ErrorKind::invalid_input,
        "invalid persisted memory is rejected");
    }
    auto empty = model->memory_state({});
    require(!empty && empty.error().kind == ErrorKind::invalid_input,
      "empty review history is rejected");
    const std::array<Review, 1> invalid_rating{{{static_cast<Rating>(5), std::chrono::days(0)}}};
    auto rating = model->memory_state(invalid_rating);
    require(!rating && rating.error().kind == ErrorKind::invalid_input,
      "unknown review ratings cannot enter the algorithm");
    auto bad_first = model->memory_state(std::span(history).subspan(1));
    require(!bad_first && bad_first.error().kind == ErrorKind::invalid_input,
      "history for a new card starts at zero elapsed days");
    const std::array<float, 1> invalid_parameters{1.F};
    auto parameters = Scheduler::create(invalid_parameters);
    require(!parameters && parameters.error().kind == ErrorKind::invalid_parameters,
      "unsupported parameter counts return typed errors");
    std::array<float, 21> nan_parameters;
    nan_parameters.fill(std::numeric_limits<float>::quiet_NaN());
    auto nan = Scheduler::create(nan_parameters);
    require(!nan && nan.error().kind == ErrorKind::invalid_parameters,
      "non-finite model parameters return typed errors");

    auto parallel = std::async(std::launch::async, [&] {
      return model->next_states({.memory = next->good.memory, .elapsed = std::chrono::days(5)});
    });
    auto simultaneous =
      model->next_states({.memory = next->good.memory, .elapsed = std::chrono::days(5)});
    auto other = parallel.get();
    require(simultaneous && other && simultaneous->good.memory == other->good.memory,
      "immutable model scheduling is deterministic across concurrent callers");
    auto moved = std::move(*model);
    auto stale = model->next_states({});
    require(!stale && stale.error().kind == ErrorKind::invalid_input && moved.next_states({}),
      "moving transfers native ownership and leaves a safe moved-from wrapper");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
