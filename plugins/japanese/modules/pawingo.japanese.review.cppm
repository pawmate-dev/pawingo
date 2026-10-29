export module pawingo.japanese.review;

import std;
export import pawingo.review_ui;
export import pawingo.japanese.voicevox;

/// Japanese question eligibility, conservative answer rules and offline speech integration.
export namespace pawingo::japanese {
/// Returns meaning plus reading/listening types for words with kanji and a usable kana reading.
std::vector<review::QuestionType> review_types(const Vocabulary &);
/// Normalizes width, kana voicing and whitespace; reading answers also fold katakana to hiragana.
/// Long vowels, small kana and sokuon remain distinct. Invalid UTF-8 yields an empty key.
std::string review_answer_key(std::string_view, bool reading);
/// Matches only this saved word's spelling/reading, or a conservative saved-meaning alternative.
MatchResult match_review(const review::Card &, std::string_view answer);
/// Creates plugin content. Speech borrows the shared service; no network or implicit downloads.
ui::ReviewContent review_content(std::shared_ptr<Voicevox>);
} // namespace pawingo::japanese
