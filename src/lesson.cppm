module;
#include <string>
#include <vector>

export module pawingo.lesson;

export namespace pawingo {
struct Card {
  std::string prompt;
  std::string answer;
  std::string note;
};

struct Lesson {
  std::string language;
  std::string title;
  std::vector<Card> cards;
};
} // namespace pawingo
