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

- Use `modules/pawingo.ui.cppm` (`import pawingo.ui;`) for semantic colors and layout constants.
  Avoid unrelated per-panel palettes.
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

- Target cross-platform behavior. Use libraries for HTTPS and hashing rather than external commands.
  Keep platform-specific process, audio, and plugin-loading code behind explicit adapters; an Arch
  development environment does not make Unix APIs a portable default.

- Use `pawingo::ui::text_input` for editable fields so empty inputs publish a correct IME cursor
  position before composition is committed.
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

## Engineering and API documentation

Use Chitin's engineering conventions as the architectural reference, adapted to C++ rather than
copied mechanically from Rust:

- Keep domain data and algorithms independent of FTXUI. Shared models belong in `pawingo.types`,
  host capabilities in `pawingo.host`, persistence in `pawingo.storage`, provider coordination in
  `pawingo.learning`, and native loading in `pawingo.plugins`. The TUI composes these layers.
- Keep library-specific transport details behind frontend-neutral request, result, and error types.
  Prefer named request fields and strong units to long positional parameter lists.
- Use `std::expected` and typed error categories for new recoverable service APIs. Convert errors
  into display text only at the frontend boundary; do not distinguish failures by parsing strings.
  Existing exception-based APIs must document what can fail.
- Express resource and worker ownership with RAII. Background jobs must have one owner, reject
  overlapping work where appropriate, propagate failures to a completion boundary, and join before
  captured state or plugin code is released.
- Use Doxygen-compatible English `///` documentation for public types and APIs, and `@file`
  summaries where they clarify a module's responsibility. A simple accessor needs one sentence.
  Complex behavior needs a summary, parameter and return semantics, failure behavior, and relevant
  cancellation, lifetime, or mutation guarantees. Avoid boilerplate sections for trivial methods.
- Document why an implementation constraint exists; do not narrate obvious statements. Keep docs
  synchronized with implementation, especially around atomic commits, cleanup, and borrowed data.
- Name focused tests by observable behavior. Assert error categories, cancellation, ownership,
  persistence, and integration contracts rather than copying implementation steps into tests.

## Configuration data

- Maintain product defaults, service resources, learning content, presentation copy, and substantial
  layout dimensions in the owning host/plugin YAML resources. Embed them through the shared xmake
  mechanism; installed software must not depend on source-tree files or the working directory.
- Keep YAML parsing behind frontend-neutral settings/contracts, validate types and bounds, and cache
  settings used by parser/inference hot paths. Preserve existing user preferences when migrating
  their storage format.
- Keep executable invariants in code: protocol/API/ABI names, Unicode encoding, SQL/ORM schema,
  units, event semantics, and hard safety ceilings. Keep build dependency declarations in xmake/Nix
  and test expectations independent of the defaults they verify.

## C++ modules

- Use named modules throughout project-owned interfaces and implementations. Put exported interfaces
  in `.cppm` files and declare the matching module in implementation units. Use `import` for project
  dependencies and `import std;` for standard-library facilities; do not add project `.hpp` facades.
- Keep third-party, C API, POSIX, and macro-dependent headers in global module fragments. Include
  third-party declarations explicitly where needed; their names are not implicitly exported through
  another module's global fragment.
- Keep `main`, test entry translation units, and native C plugin adapters as small global-module
  boundaries that import project modules. Generated YAML and SQL resources must also be modules.
- Declare public module interfaces on the owning xmake library target and reuse it through target
  dependencies. Do not recompile the same service implementation into every test executable.
- Run `just check-modules` and the normal build/tests. Treat named-module ownership as part of the
  plugin C++ ABI and reject incompatible pre-module binaries through the descriptor tag.
