# Architecture and contracts

Pawingo follows Chitin's dependency direction and proportionate API documentation conventions,
adapted to C++26 and native FTXUI plugins.

The host redirects process stderr to the append-only `logs/pawingo.log` beneath its data directory
before loading plugins. `pawingo.io::DiagnosticLog` captures native runtime diagnostics without
changing stdout or the terminal background. Its scope outlives plugin workers and restores stderr
before startup failures reach the outer exception handler. The relative log path belongs to
`diagnostics.file` in the embedded application YAML. Redirection has explicit POSIX and Windows CRT
adapters and is owned by the host, rather than toggled around individual asynchronous operations.

## Named-module layers

Project interfaces are C++26 named modules, not textual headers. Public interfaces live in
`modules/*.cppm` and `plugins/japanese/modules/*.cppm`. Implementation units declare the same module
as their interface; project dependencies use `import` or `export import`.

| Module                                                  | Responsibility                                                                         |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| `pawingo.types`, `pawingo.host`                         | Shared vocabulary/resource models and borrowed host services                           |
| `pawingo.plugin`                                        | Native plugin descriptor, language contract, and optional panel help                   |
| `pawingo.config`                                        | Immutable, frontend-neutral configuration and user paths                               |
| `pawingo.io`, `pawingo.tasks`                           | Temporary artifacts and cancellable worker ownership                                   |
| `pawingo.net`                                           | HTTPS contracts and libcurl implementation                                             |
| `pawingo.storage`, `pawingo.learning`, `pawingo.review` | SQLite persistence, provider orchestration, and answer/review algorithms               |
| `pawingo.plugins`                                       | Native library discovery and lifetime                                                  |
| `pawingo.ui`                                            | Shared FTXUI theme and presentation helpers                                            |
| `pawingo.core`                                          | Re-exported host API convenience module                                                |
| `pawingo.japanese.*`                                    | Japanese settings, dictionary, speech, installation, panels, and plugin implementation |

`pawingo-runtime` compiles the shared module interfaces and implementations once as a position-
independent static library. `japanese-backend` owns Japanese modules and resources. xmake exports
public module interfaces through target dependencies. `pawingo` imports the runtime; the `japanese`
shared-library target contains only the native C entry adapter and links the backend. Tests import
the same interfaces and link these libraries rather than recompiling service sources.

Use `import std;` for the standard library. Third-party headers that do not supply the needed
standard module interfaces remain in global module fragments. This includes FTXUI, SQLite ORM,
yaml-cpp, libcurl, miniaudio, and the VOICEVOX C API. Importing a project module does not make
unexported FTXUI header names visible; a unit using those names includes its third-party headers
explicitly in its global module fragment. Native C/POSIX macro and platform declarations stay at
that same boundary.

Executable `main` functions, test entry translation units, and native `extern "C"` plugin adapters
remain in the global module and import project modules. These are entry boundaries, not a second
header-based API. `just check-modules` enforces project imports and module declarations and runs
before `just test`.

The ABI tag includes `modules-1`. Existing header-based plugin binaries must be rebuilt: named-
module ownership changes C++ linkage and RTTI. The native `pawingo_plugin_v1` descriptor entry shape
is unchanged, but its ABI tag rejects old binaries. Native entry adapters explicitly export their C
symbol (`dllexport` on Windows and default symbol visibility on ELF/Mach-O); C++ module `export`
alone does not export a symbol from a shared library. Compiler, standard library, flags, and FTXUI
compatibility still apply; BMIs are compiler-specific build artifacts.

## Text input and IME positioning

Use `pawingo::ui::text_input` for editable fields. FTXUI 6.1.9's empty-input placeholder path has no
hardware cursor shape; the screen can consequently park the cursor at its bottom-right corner while
the input method is composing uncommitted text. The shared adapter anchors that focused empty state
to one cell at the input's start. Placeholder painting, dimensions, and mouse hitboxes are
preserved. Once content exists, native FTXUI cursor movement and scrolling apply, including CJK
glyph widths and externally borrowed insert-mode state.

## Lifetimes and failure handling

The host outlives each panel. Each panel releases its background job before destroying captured
state. Jobs catch work failures and deliver them to a completion callback; callbacks must not throw
or reenter the job. Panel teardown cancels and joins the worker before native libraries are closed.

Download requests are bounded by bytes and elapsed time. Successful receipts contain the closed
file's path, byte count, and SHA-256. Invalid or pre-cancelled requests do not touch the
destination; failures after opening it remove the partial file. Resource workflows choose a private
scratch path, validate the artifact, and then commit it separately. HTTP status and failure
categories remain available to callers; the frontend decides how to present them.

Legacy persistence and dictionary APIs currently propagate exceptions. Their public contracts
identify validation, parsing, filesystem, and cancellation behavior. New transport APIs use typed
results; exception-to-text conversion is restricted to UI/task completion boundaries.

## Portability boundary

Domain models, dictionary parsing contracts, worker ownership, and download contracts do not use
terminal APIs. Native library loading and audio process execution still contain POSIX adapters; this
separation does not imply that the entire application already builds on Windows. Those adapters
require platform-specific implementations and platform CI before making that claim.

## Embedded Japanese voice resources

`plugins/japanese/resources/voicevox.yaml` owns the offline bundle version, supported platform
mappings, resource URLs, checksums, limits, and resource discovery names. The matching runtime terms
live in `voicevox-runtime-terms.txt`. Keep upstream notices verbatim when updating a bundle.

The xmake `embed_voicevox` hook generates `build/generated/pawingo_voicevox_assets.cppm` for the
Japanese backend. It exports the `pawingo.japanese.assets` module. Changed resource contents rewrite
the generated interface, causing normal compiler dependency tracking to rebuild its consumers. The
generated module is a build artifact; maintain the YAML and text sources instead. Installed plugins
do not read project files.

`voicevox_manifest.cpp` uses yaml-cpp to validate schema 1 and resolve platform variables into typed
installation data. Malformed settings and unsupported platforms fail before network access.
`voicevox_install.cpp` consumes this plan and retains responsibility for cancellation, archive
restrictions, candidate ownership, and atomic installation. Runtime user paths and selected style
IDs remain separate preferences in `voicevox.yaml`. Existing `voicevox.json` files remain readable
as a fallback; saving writes YAML and preserves the legacy file.

## FSRS scheduling boundary

`pawingo.review` wraps the published `fsrs` crate at exactly **6.6.2** (FSRS-6). The crate version
and transitive dependencies are pinned in `native/fsrs/Cargo.toml` and `Cargo.lock`. The old
consecutive-success interval doubling API has been removed; conservative answer normalization
remains available independently of recall ratings.

`Scheduler::create()` returns an owning, move-only model through `std::expected`. An empty parameter
span selects upstream algorithm defaults; custom finite parameter arrays of 17, 19 or 21 values use
upstream conversion and clipping. The product's default desired retention is embedded as
`review.desired_retention` in application YAML; each `ScheduleRequest` may override it.

```cpp
import std;
import pawingo.review;

auto scheduler = pawingo::review::Scheduler::create();
if (!scheduler)
  return; // Present scheduler.error() at the frontend boundary.
auto choices = scheduler->next_states({}); // A new card, with zero elapsed days.
if (!choices)
  return;
auto memory = choices->good.memory; // Persist only the outcome actually selected by the user.
auto following = scheduler->next_states({
  .memory = memory,
  .elapsed = std::chrono::days(5),
  .desired_retention = 0.95F,
});
```

`MemoryState` holds stability in fractional days and difficulty. `next_states` returns Again, Hard,
Good and Easy outcomes without changing the prior state. Elapsed times are nonnegative whole days,
matching this pinned upstream API; intervals retain their original floating-point precision through
the strong `Days` duration. `memory_state` rebuilds state from chronological ratings and elapsed-day
records, optionally continuing a stored state. A new card's first elapsed interval is zero. These
operations are immutable and safe for concurrent callers while the owning model remains alive.

`native/fsrs` is a small Rust static-library adapter, not a second application backend. Its private
versioned C functions validate inputs, return numeric error categories and contain unwinding panics.
Results are committed into caller-owned structures only on success; the opaque model is released by
C++ RAII using its matching Rust destructor. Invalid pointers, allocation aborts and undefined
behavior are outside panic recovery. All unsafe pointer operations document their lifetime and
aliasing requirements. The generated `native/fsrs/include/pawingo_fsrs.h` is a private C ABI
boundary, not a project C++ header facade; only the implementation unit includes it in a global
module fragment. Cargo regenerates this checked-in declaration from Rust with cbindgen; do not edit
it by hand.

xmake's `pawingo-fsrs` target builds the locked Cargo graph for its configured
platform/architecture, using PIC and a matching debug/release profile, then supplies the archive
through target dependencies. Unchanged sources reuse the archive. A first build downloads Cargo
dependencies; subsequent builds can run offline once cached. No Rust shared library or Cargo
installation is needed at runtime. The Nix development shell includes the Rust toolchain.
`just test` runs both Rust boundary tests and C++ module integration tests; `just fmt` also formats
the Rust sources. Run strict Rust linting with:

```sh
cargo clippy --manifest-path native/fsrs/Cargo.toml --target-dir build/fsrs --all-targets --all-features --locked -- -D warnings
```

The module is deliberately independent of persistence and TUI flows. A review queue must still apply
learning/relearning steps, interval rounding and caps, due timestamps, daily limits and
transactional state/log updates. Match results and recall ratings must be stored independently.
Existing vocabulary and review rows are preserved; this adapter does not infer missing historical
ratings or mutate stored schedules. Parameter optimization is not yet exposed by the adapter.

## Configuration ownership and hardcoding audit

Product defaults, learning data, and presentation copy belong in build-time YAML resources:

| Source                                     | Owns                                                                                                                                                                  |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `resources/application.yaml`               | Storage names, network defaults, consent default, theme, selection marks, activity calendar, FSRS desired retention, language names/flags, host copy                  |
| `plugins/japanese/resources/defaults.yaml` | Plugin metadata, dictionary source and limits, target language and gloss aliases, TTS defaults and legacy adapters, resource paths, panel dimensions, archive budgets |
| `plugins/japanese/resources/kana.yaml`     | Kana cells, row/column labels, initial selection                                                                                                                      |
| `plugins/japanese/resources/messages.yaml` | Japanese panel titles, controls, shortcuts, confirmations, and ordinary status text                                                                                   |
| `plugins/japanese/resources/voicevox.yaml` | Pinned offline voice installation plan                                                                                                                                |

The shared xmake `embed_assets` mechanism embeds these sources in generated modules. The
`pawingo.config` module in the `pawingo-runtime` static library provides an immutable
frontend-neutral `Settings` API; YAML types remain private to its implementation. Host defaults and
Japanese defaults have separate ownership. Resource changes trigger recompilation; installed
binaries do not need the source tree or YAML files at runtime. Editing bundled defaults requires
rebuilding. User-editable voice paths and style selection use the per-user YAML preference file and
do not require rebuilding.

The audit intentionally preserves executable contracts in their native formats: C API names and
plugin ABI symbols, database/ORM field names and independent SQL migrations, XML vocabulary and its
specified defaults, Unicode encoding rules, mathematical unit conversions, keyboard event handling,
small structural whitespace/spacing operations, and validation ceilings. These are implementation
invariants rather than product defaults. Error diagnostics remain beside the code that raises them.
Tests retain explicit fixtures and expected values so they can detect accidental changes in bundled
behavior. Build dependencies and their versions remain declarative in xmake and the Nix lock file;
YAML is not used as an additional build-system language.

SQL migrations likewise generate `pawingo.assets.sql`; application defaults generate
`pawingo.assets.application`. Resource consumers import these modules. Generated modules are private
implementation dependencies; installed programs require neither generated source files nor the
original YAML/SQL files.
