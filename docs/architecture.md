# Architecture and contracts

Pawingo follows Chitin's dependency direction and proportionate API documentation conventions,
adapted to C++26 and native FTXUI plugins.

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

## Configuration ownership and hardcoding audit

Product defaults, learning data, and presentation copy belong in build-time YAML resources:

| Source                                     | Owns                                                                                                                                                                  |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `resources/application.yaml`               | Storage names, network defaults, consent default, theme, selection marks, activity calendar, review intervals, language names/flags, host copy                        |
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
