module;
#include <string>
#include <vector>

export module pawingo.japanese;
import pawingo.lesson;

export namespace pawingo::japanese {
Lesson beginner() {
  return {"Japanese",
    "Everyday greetings",
    {
      {"こんにちは", "Hello", "konnichiwa"},
      {"ありがとう", "Thank you", "arigatou"},
      {"おはよう", "Good morning", "ohayou"},
      {"さようなら", "Goodbye", "sayounara"},
    }};
}
} // namespace pawingo::japanese
