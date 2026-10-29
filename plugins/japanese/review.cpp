module;
module pawingo.japanese.review;

import std;
import pawingo.japanese.settings;
import pawingo.japanese.voicevox;

namespace pawingo::japanese {
namespace {
std::u32string decode(std::string_view text) {
  std::u32string result;
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    char32_t cp = first;
    int count = 0;
    if (first >= 0xC2 && first <= 0xDF) {
      cp &= 0x1F;
      count = 1;
    } else if (first >= 0xE0 && first <= 0xEF) {
      cp &= 0x0F;
      count = 2;
    } else if (first >= 0xF0 && first <= 0xF4) {
      cp &= 0x07;
      count = 3;
    } else if (first >= 0x80)
      return {};
    if (i + count > text.size())
      return {};
    for (int j = 0; j < count; ++j) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xC0) != 0x80)
        return {};
      cp = (cp << 6) | (next & 0x3F);
    }
    if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) ||
      cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      return {};
    result.push_back(cp);
  }
  return result;
}

std::string encode(std::u32string_view text) {
  std::string result;
  for (char32_t cp : text) {
    if (cp < 0x80)
      result += static_cast<char>(cp);
    else if (cp < 0x800) {
      result += static_cast<char>(0xC0 | (cp >> 6));
      result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      result += static_cast<char>(0xE0 | (cp >> 12));
      result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      result += static_cast<char>(0xF0 | (cp >> 18));
      result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return result;
}

bool kanji(std::u32string_view text) {
  return std::ranges::any_of(text, [](char32_t cp) {
    return (cp >= 0x3400 && cp <= 0x9FFF) || (cp >= 0x20000 && cp <= 0x323AF);
  });
}
} // namespace

std::string review_answer_key(std::string_view text, bool reading) {
  // Unicode's contiguous halfwidth-katakana block maps one-to-one before voicing composition.
  constexpr std::u32string_view half =
    U"。「」、・ヲァィゥェォャュョッーアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホ"
    U"マミムメモヤユヨラリルレロワン゙゚";
  std::u32string result;
  for (char32_t cp : decode(text)) {
    if (cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == 0x3000)
      continue;
    if (cp >= 0xFF61 && cp <= 0xFF9F)
      cp = half[cp - 0xFF61];
    if (cp >= 0xFF01 && cp <= 0xFF5E)
      cp -= 0xFEE0;
    // Compose Japanese dakuten/handakuten, preserving all other spelling distinctions.
    if ((cp == 0x3099 || cp == 0x309A) && !result.empty()) {
      constexpr std::u32string_view bases =
        U"かきくけこさしすせそたちつてとはひふへほうカキクケコサシスセソタチツテトハヒフヘホウ";
      constexpr std::u32string_view voiced =
        U"がぎぐげござじずぜぞだぢづでどばびぶべぼゔガギグゲゴザジズゼゾダヂヅデドバビブベボヴ";
      const auto found = bases.find(result.back());
      if (cp == 0x3099 && found != std::u32string_view::npos) {
        result.back() = voiced[found];
        continue;
      }
      constexpr std::u32string_view h = U"はひふへほハヒフヘホ", p = U"ぱぴぷぺぽパピプペポ";
      const auto semi = h.find(result.back());
      if (cp == 0x309A && semi != std::u32string_view::npos) {
        result.back() = p[semi];
        continue;
      }
    }
    result.push_back(cp);
  }
  if (reading)
    for (auto &cp : result)
      if (cp >= 0x30A1 && cp <= 0x30F6)
        cp -= 0x60;
  return encode(result);
}

std::vector<review::QuestionType> review_types(const Vocabulary &word) {
  using enum review::QuestionType;
  const auto key = decode(review_answer_key(word.reading, true));
  const bool kana = !key.empty() && std::ranges::all_of(key, [](char32_t cp) {
    return (cp >= 0x3041 && cp <= 0x3096) || cp == 0x30FC;
  });
  if (kanji(decode(word.expression)) && kana)
    return {meaning, reading, listening};
  return {meaning};
}

MatchResult match_review(const review::Card &card, std::string_view answer) {
  if (card.kind == review::QuestionType::meaning)
    return review::match_meaning(card.word, answer);
  const bool reading = card.kind == review::QuestionType::reading;
  const auto &expected = reading ? card.word.reading : card.word.expression;
  const auto key = review_answer_key(answer, reading);
  if (!key.empty() && key == review_answer_key(expected, reading))
    return {answer == expected ? Match::correct : Match::acceptable, {}};
  return {Match::incorrect, {}};
}

ui::ReviewContent review_content(std::shared_ptr<Voicevox> speech) {
  ui::ReviewContent result;
  result.types = review_types;
  result.match = [](const review::Card &card, const std::string &answer) {
    return match_review(card, answer);
  };
  result.question = [](const review::Card &card) {
    const auto &word = card.word;
    switch (card.kind) {
    case review::QuestionType::reading:
      return ui::ReviewQuestion{word.expression, "", word.reading, ui_text("review_reading")};
    case review::QuestionType::listening:
      return ui::ReviewQuestion{ui_text("review_listen"),
        word.meaning,
        word.expression,
        ui_text("review_spelling"),
        true};
    default:
      return ui::ReviewQuestion{word.expression,
        word.reading,
        word.meaning,
        ui_text("review_meaning")};
    }
  };
  result.speak = [speech = std::move(speech)](const Vocabulary &word,
                   std::stop_token stop) -> std::expected<void, review::Error> {
    if (!speech)
      return std::unexpected(review::Error{review::ErrorKind::configuration,
        "No speech service was supplied."});
    auto config = Voicevox::load_config();
    if (!config)
      return std::unexpected(review::Error{review::ErrorKind::configuration,
        config.error().message});
    auto played = speech->speak(*config, SpeechRequest{word.expression, word.reading}, stop);
    if (!played)
      return std::unexpected(review::Error{review::ErrorKind::backend, played.error().message});
    return {};
  };
  return result;
}
} // namespace pawingo::japanese
