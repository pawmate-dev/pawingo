module;
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

export module pawingo.ui;

import std;
export import pawingo.config;

export namespace pawingo::ui {
/// Creates a native input with an IME cursor anchored at its first cell while empty.
/// Borrowed content must outlive the returned component. Native editing, scrolling and insert-mode
/// handling are preserved; placeholder layout and text are unchanged. Nonempty inputs use FTXUI's
/// normal glyph-aware cursor. This workaround targets the empty-input path in FTXUI 6.1.9.
ftxui::Component
text_input(std::string *content, std::string placeholder, ftxui::InputOption options = {});

// Keep the terminal background transparent; colors carry semantic emphasis.
inline ftxui::Color theme_color(const char *name) {
  auto values = config::application().integers(std::string("theme.colors.") + name);
  if (values.size() != 3 || std::ranges::any_of(values, [](int value) {
        return value < 0 || value > 255;
      }))
    throw std::runtime_error("Invalid theme RGB color.");
  return ftxui::Color::RGB(values[0], values[1], values[2]);
}

inline const auto ink = theme_color("ink");
inline const auto secondary = theme_color("secondary");
inline const auto muted = theme_color("muted");
inline const auto faint = theme_color("faint");
inline const auto olive = theme_color("olive");
inline const auto accent = theme_color("accent");
inline const int content_width = config::application().integer("theme.content_width", 40, 200);

/// Returns the configured selection glyph and its reserved padding.
inline std::string selection(bool selected, bool field = false) {
  const auto marks =
    config::application().strings(field ? "theme.field_selection" : "theme.selection");
  if (marks.size() != 2)
    throw std::runtime_error("Selection markers require active and inactive entries.");
  return marks[selected ? 0 : 1];
}

inline ftxui::Element wordmark() {
  return ftxui::text(config::application().text("theme.wordmark")) | ftxui::color(secondary) |
    ftxui::hcenter;
}
} // namespace pawingo::ui
