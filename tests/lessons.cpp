#include <iostream>
#include <string>

import pawingo.lesson;
import pawingo.japanese;
import pawingo.german;

int main() {
  const auto japanese = pawingo::japanese::beginner();
  const auto german = pawingo::german::beginner();
  if (japanese.language != "Japanese" || german.language != "German") {
    std::cerr << "Unexpected language name\n";
    return 1;
  }
  for (const auto *lesson : {&japanese, &german}) {
    if (lesson->cards.empty()) {
      std::cerr << "Lesson has no cards\n";
      return 1;
    }
    for (const auto &card : lesson->cards) {
      if (card.prompt.empty() || card.answer.empty()) {
        std::cerr << "Card is missing its prompt or answer\n";
        return 1;
      }
    }
  }
}
