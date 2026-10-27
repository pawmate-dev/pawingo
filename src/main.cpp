#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

import pawingo.lesson;
import pawingo.japanese;
import pawingo.german;

int main() {
  using namespace ftxui;
  std::vector<pawingo::Lesson> lessons{pawingo::japanese::beginner(), pawingo::german::beginner()};
  std::vector<std::string> languages{"Japanese", "German"};
  int selected = 0;
  int card = 0;
  bool revealed = false;
  auto screen = ScreenInteractive::TerminalOutput();
  auto menu = Menu(&languages, &selected);
  auto reveal = Button("Reveal / hide", [&] {
    revealed = !revealed;
  });
  auto next = Button("Next card", [&] {
    card = (card + 1) % static_cast<int>(lessons[selected].cards.size());
    revealed = false;
  });
  auto quit = Button("Quit", screen.ExitLoopClosure());
  auto controls = Container::Vertical({menu, reveal, next, quit});
  int previous = selected;
  auto app = Renderer(controls, [&] {
    if (previous != selected) {
      previous = selected;
      card = 0;
      revealed = false;
    }
    const auto &lesson = lessons[selected];
    const auto &current = lesson.cards[card];
    auto content = vbox({
      text("Pawingo") | bold | center,
      text("Choose a language, then study its cards.") | center,
      separator(),
      menu->Render(),
      separator(),
      text(lesson.language + " — " + lesson.title) | bold,
      text("Card " + std::to_string(card + 1) + " / " + std::to_string(lesson.cards.size())),
      text(current.prompt) | bold,
      text(revealed ? current.answer : "Answer hidden"),
      text(revealed ? current.note : ""),
      separator(),
      reveal->Render(),
      next->Render(),
      quit->Render(),
      text("Tab: switch control  •  Enter: select") | dim,
    });
    return content | border;
  });
  screen.Loop(app);
}
