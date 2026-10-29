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

The first question type shows the word and its reading and asks for its saved meaning. The small
language code identifies the definition language captured when the word was saved. A word saved with
German meanings asks for German, independently of other saved English definitions.

1. Type an answer and press Enter to check it.
2. Read the expected meaning and the proposed review intervals. Up/Down scrolls long meanings.
3. Use Left/Right to choose **again**, **hard**, **good** or **easy**, then Enter to confirm. The
   keys `1` through `4` commit the corresponding rating directly from feedback.
4. The next due or new card appears. When none remain, Enter refreshes the queue.

Matching accepts the complete saved definition or one semicolon-delimited alternative, with
conservative whitespace and ASCII case normalization. For example, `cat` and `feline` both answer
`cat; feline`. It does not guess synonyms, accept partial strings, strip parenthetical qualifiers or
perform Unicode-wide case folding. The loaded language backend can provide its own assessment;
saved-meaning matching also works when that backend is unavailable.

A recognized answer initially selects **good**; an unmatched or blank answer selects **again**.
These are suggestions. If the automated assessment missed a valid synonym, choose the recall rating
yourself. Matching outcome and recall rating are recorded separately. Checking an answer alone
records nothing; only explicit rating confirmation updates learning history.

Escape from feedback returns to the input with the draft preserved. Escape from the question or
empty queue returns to the language feature list without grading it. New-card enrollment creates no
learning activity. Actual committed reviews update the home calendar, including repeated same-day
practice.

## Scheduling and persistence

The queue uses the pinned `fsrs-rs 6.6.2` implementation. Stability, difficulty, completion time,
due time and a revision are persisted per saved meaning card. Again retains FSRS's updated memory
but uses a short relearning step; the other ratings use rounded, bounded FSRS intervals. Same-day
relearning passes zero elapsed days to the model and continues the persisted state.

Defaults live in the embedded `review` section of `resources/application.yaml`:

- Desired retention: **0.9**.
- Again delay: **600 seconds**.
- Minimum/maximum daily interval: **1 / 36500 days**.
- Queue batch size: **50**. Further batches load as the session progresses; this is not a daily cap.

Editing bundled defaults requires rebuilding. Intervals use elapsed whole 24-hour days, while the
home calendar groups activity by local date. A future-due card stays out of the queue after restart.
An Again card becomes available when its short due time arrives; refresh or reopen Review then.

The schema additions live in `sql/review.sql`. Ordinary persistence uses sqlite_orm. Updating the
card, appending its FSRS event and recording activity are one transaction. A failed write leaves the
feedback available for retry; a stale session reloads the queue without duplicating a review. Saving
the same favorite again does not reset its memory. Older activity records lack FSRS ratings and are
retained as statistics rather than converted into invented rating histories.

This first session type reviews meanings. Reading/spelling cards, sentence cards, daily caps,
automatic parameter training and multi-step learning ladders are not included yet.
