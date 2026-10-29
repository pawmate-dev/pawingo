module;
#include <ftxui/dom/elements.hpp>

module pawingo.ui;

import std;

namespace pawingo::ui {
namespace {
struct HeatPolicy {
  std::vector<int> thresholds;
  std::vector<ftxui::Color> colors;
  std::vector<std::string> marks;
};

const HeatPolicy &heat_policy() {
  static const auto policy = [] {
    const auto &settings = config::application();
    HeatPolicy value;
    value.thresholds = settings.integers("activity.thresholds");
    const auto names = settings.strings("activity.level_colors");
    value.marks = settings.strings("activity.marks");
    if (value.thresholds.empty() || value.thresholds.size() > 10 || value.thresholds.front() != 1 ||
      names.size() != value.thresholds.size() + 1 || value.marks.size() != names.size())
      throw std::runtime_error(
        "Activity levels require positive thresholds and matching colors/marks."
      );
    for (std::size_t i = 1; i < value.thresholds.size(); ++i)
      if (value.thresholds[i] <= value.thresholds[i - 1])
        throw std::runtime_error("Activity thresholds must be strictly increasing.");
    for (const auto &name : names)
      value.colors.push_back(theme_color(name.c_str()));
    return value;
  }();
  return policy;
}
} // namespace

std::size_t activity_level(std::size_t learned_words) {
  const auto &policy = heat_policy();
  return static_cast<std::size_t>(std::upper_bound(policy.thresholds.begin(),
                                    policy.thresholds.end(),
                                    learned_words,
                                    [](std::size_t count, int threshold) {
                                      return count < static_cast<std::size_t>(threshold);
                                    }) -
    policy.thresholds.begin());
}

ftxui::Element activity_dot(std::size_t learned_words) {
  const auto &policy = heat_policy();
  const auto level = activity_level(learned_words);
  return ftxui::text(policy.marks[level]) | ftxui::color(policy.colors[level]);
}
} // namespace pawingo::ui
