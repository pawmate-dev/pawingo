# Reviewing saved vocabulary

Select a language on the home screen, then open its review feature. For Japanese, choose **Japanese
→ 復習**. The home screen has no global Review entry or `r` shortcut. Each plugin owns its entry,
answer matcher and panel lifetime; the host provides the shared offline FSRS session. Every queue
load, advance and refresh stays within the plugin's vocabulary language code. A Japanese word with
German definitions remains in Japanese review, not German review.

Existing favorites retain their vocabulary, IDs, schedules, sources and historical activity.
Language separation uses the existing vocabulary language field in the shared database; no data
migration or separate database is needed. An unavailable plugin's cards remain stored until that
plugin is installed again.

Japanese creates three independent card types for words containing kanji with a usable saved kana
reading. Kana-only words and words without a reading keep meaning cards only.

| Type      | Question                                               | Answer                                             |
| --------- | ------------------------------------------------------ | -------------------------------------------------- |
| Meaning   | Written word and reading                               | A saved meaning or semicolon-delimited alternative |
| Reading   | Written word; reading is hidden                        | The saved kana reading                             |
| Listening | Audio cue and meaning; spelling and reading are hidden | The saved written form, including kanji            |

The definition-language code identifies the target captured when saving. Listening meanings provide
homophone context. Press Ctrl+P to listen/replay before checking a listening answer; successful
audio playback is required, so missing voice resources and playback failures never become failed
recall records. Configure VOICEVOX in the plugin's voice settings; review never downloads resources.
Speech is asynchronous, rejects overlapping requests and is cancelled/joined when the panel closes.
A reading question cannot be spoken before checking, since that would reveal its answer; Ctrl+P is
available in feedback and on meaning questions.

1. Type an answer and press Enter to check it.
2. Read the expected answer and the proposed review intervals. Up/Down scrolls long meanings.
3. Use Left/Right to choose **again**, **hard**, **good** or **easy**, then Enter to confirm. The
   keys `1` through `4` commit the corresponding rating directly from feedback.
4. The next due or new card appears. When none remain, Enter refreshes the queue.

Meaning matching accepts the complete saved definition or one semicolon-delimited alternative, with
conservative whitespace and ASCII case normalization. For example, `cat` and `feline` both answer
`cat; feline`. It does not guess synonyms, accept partial strings, strip parenthetical qualifiers or
perform Unicode-wide case folding.

Japanese reading answers normalize ordinary hiragana/katakana, halfwidth kana, fullwidth ASCII,
Japanese combining voicing marks and whitespace. Sokuon, small kana and long vowels stay distinct:
`きて` does not answer `きって`, and `おばさん` does not answer `おばあさん`. Romaji is not accepted
as kana. Listening answers normalize width/voicing/whitespace but require the saved spelling; `ねこ`
does not answer `猫`. Only the saved reading is accepted: review does not infer additional readings
from unrelated dictionary senses or accept arbitrary homophones. During feedback, terracotta
highlights the changed part of an incorrect answer and olive highlights its correction. Equivalent
accepted forms are not marked as errors. Long answers and meanings remain scrollable.

A recognized answer initially selects **good**; an unmatched or blank answer selects **again**.
These are suggestions. If the automated assessment missed a valid synonym, choose the recall rating
yourself. Matching outcome and recall rating are recorded separately. Checking an answer alone
records nothing; only explicit rating confirmation updates learning history.

Escape from feedback returns to the input with the draft preserved. Escape from the question or
empty queue returns to the language feature list without grading it. New-card enrollment creates no
learning activity. The home calendar counts distinct reviewed words per local date, across
languages. A word is identified by source language, spelling and reading. Repeating the word or
reviewing its other question types/definition languages/sources on the same day does not increase
its daily count; reviewing it on another day counts again. Any confirmed recall rating, including
Again, counts as study; searching, saving, audio playback and answer previews do not.

The calendar uses five foreground shades: 0, 1–4, 5–9, 10–19 and 20+ words. Levels reuse the
original faint, warm gray, olive and warm ivory theme colors. Today uses the same count-based color
and glyph as every other date, with no special ring. Thresholds, semantic colors and glyphs live in
embedded application YAML. Future days remain blank and the terminal background stays transparent.

## Scheduling and persistence

The queue uses the pinned `fsrs-rs 6.6.2` implementation. Stability, difficulty, completion time,
due time and a revision are persisted per vocabulary/question-type pair. Again retains FSRS's
updated memory but uses a short relearning step; the other ratings use rounded, bounded FSRS
intervals. Same-day relearning passes zero elapsed days to the model and continues the persisted
state.

Defaults live in the embedded `review` section of `resources/application.yaml`:

- Desired retention: **0.9**.
- Again delay: **600 seconds**.
- Minimum/maximum daily interval: **1 / 36500 days**.
- Queue batch size: **50**. Further batches load as the session progresses; this is not a daily cap.

Editing bundled defaults requires rebuilding. Intervals use elapsed whole 24-hour days, while the
home calendar groups activity by local date. A future-due card stays out of the queue after restart.
An Again card becomes available when its short due time arrives; refresh or reopen Review then.

`sql/review.sql` enrolls original meaning cards; `sql/review_variants.sql` atomically upgrades the
schema to independent question types. Existing meaning states, review IDs, timestamps, answers and
FSRS history remain intact; new types start without inferred memory. Reopening and resaving cannot
reset an existing card. Ordinary persistence uses sqlite_orm. Updating the card, appending its FSRS
event and recording activity are one transaction. A failed write leaves the feedback available for
retry; a stale session reloads the queue without duplicating a review. Saving the same favorite
again does not reset its memory. Older activity records lack FSRS ratings and are retained as
statistics rather than converted into invented rating histories.

The queue mixes due/new question types in stable order. Daily caps, sibling burying, undo, sentence
cards, automatic parameter training and multi-step learning ladders are not included yet.
