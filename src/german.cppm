module;
#include <string>
#include <vector>

export module pawingo.german;
import pawingo.lesson;

export namespace pawingo::german {
Lesson beginner() {
  return {"German",
    "Everyday greetings",
    {
      {"Hallo", "Hello", "A common informal greeting"},
      {"Danke", "Thank you", "A common way to say thanks"},
      {"Guten Morgen", "Good morning", "A greeting used in the morning"},
      {"Auf Wiedersehen", "Goodbye", "A formal farewell"},
    }};
}
} // namespace pawingo::german
