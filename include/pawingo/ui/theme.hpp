#pragma once
#include <ftxui/dom/elements.hpp>

namespace pawingo::ui {
// Keep the terminal background transparent; colors carry semantic emphasis.
inline const auto ink = ftxui::Color::RGB(201, 194, 171);
inline const auto secondary = ftxui::Color::RGB(145, 139, 120);
inline const auto muted = ftxui::Color::RGB(112, 107, 93);
inline const auto faint = ftxui::Color::RGB(64, 67, 55);
inline const auto olive = ftxui::Color::RGB(139, 150, 112);
inline const auto accent = ftxui::Color::RGB(211, 101, 69);
inline constexpr int content_width = 72;
inline ftxui::Element wordmark() {
  return ftxui::text("P a w i n g o") | ftxui::color(secondary) | ftxui::hcenter;
}
} // namespace pawingo::ui
