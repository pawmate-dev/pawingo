# Pawingo project guidance

## Collaboration and documentation

- Respond to the user in Chinese unless they request another language.
- Write documentation, code comments, and development instructions in English. Native language
  labels and learning content are welcome in the product.
- Do not create or edit README files unless explicitly requested.

## Visual language

Pawingo is a quiet, spacious terminal learning environment. Its visual references use muted olive
activity dots, warm text, a small terracotta accent, and generous negative space. Treat this as a
shared product language across the host and every plugin.

- Use `include/pawingo/ui/theme.hpp` for semantic colors and layout constants. Avoid unrelated
  per-panel palettes.
- Preserve the terminal's background and transparency. Do not impose a background image or an opaque
  page fill.
- Center the main content as a stable group. Use explicit content widths, bounded status regions,
  and wrapping text so messages cannot shift a chart or change its horizontal alignment.
- Keep the wordmark small, lightly spaced, and understated. Use weight sparingly; hierarchy should
  come mainly from spacing and color.
- Favor whitespace over borders, boxes, separators, or dashboard chrome. Do not add promotional
  copy, slogans, redundant section headings, or verbose selected-item labels.
- Use warm ivory for important text, secondary warm gray for ordinary content, muted gray for hints,
  olive for learning activity, and terracotta for the current selection. Reserve accent color for
  small, purposeful marks.
- Indicate selection with a small dot or the selected glyph's color. Avoid full-row inverse
  highlighting and large solid selection rectangles.
- Render the learning calendar as separated dots: faint dots for no activity, olive rings for light
  activity, and warm filled dots for heavier activity. A terracotta ring may identify today. Never
  manufacture learning records to make the calendar look populated.
- Present language choices with native names and subdued English names. When country flags are used,
  keep genuine Unicode flag emoji; do not replace them with country-code text or improvised graphics
  without a user request.
- Learning tables should be airy and aligned, with small row and column labels. Keep hiragana and
  katakana visible together. Follow visual navigation rather than introducing unnecessary script
  toggles.
- Pin concise, lowercase keyboard hints to the screen bottom, outside the centered content group.
  Use Escape consistently to dismiss the current prompt or return one level. A modal prompt consumes
  Escape before page navigation.
- Reference screenshots express visual style, not permission to change established keyboard behavior
  or remove required learning content.

## Interaction and implementation

- Use the alternate terminal buffer for the full-screen TUI and restore the terminal on exit.
- Keep input responsive during speech, downloads, and other slow operations. Workers must be
  cancellable and joined before a plugin is unloaded.
- Reuse installed resources. Network access and downloads require a clear user action or the
  configured service policy; a missing runtime dictionary is not a reason to download an existing
  voice again.
- Share layout and style mechanisms between the host and plugins. Keep language-specific learning
  behavior inside the corresponding plugin.
- Keep model status and confirmation text inside a reserved region; use the fixed footer for
  shortcuts.

## Verification

- Build changes with xmake and use the existing just recipes.
- For navigation or layout changes, verify the real terminal flow, including Escape, the fixed
  footer, and status messages at representative terminal sizes.
- Verify asynchronous playback, cancellation, and installed-resource reuse when changing speech
  behavior.
- Keep database migrations in independent SQL files. Use the existing ORM for ordinary persistence
  operations.
