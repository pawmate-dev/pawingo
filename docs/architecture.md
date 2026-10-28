# Architecture and contracts

Pawingo follows Chitin's dependency direction and proportionate API documentation conventions,
adapted to C++26 and native FTXUI plugins.

## Layers

- `include/pawingo/types.hpp` contains frontend-neutral vocabulary, assessment, and resource data.
- `include/pawingo/host.hpp` describes borrowed persistence/resource services and redraw requests.
- `include/pawingo/storage/` and `src/storage/` own SQLite persistence.
- `include/pawingo/learning/` and `src/learning/` coordinate local providers and service consent.
- `include/pawingo/plugins/` and `src/plugins/` own native plugin discovery and library lifetime.
- `include/pawingo/net/` and `src/download.cpp` define transport-neutral download contracts and
  implement the libcurl adapter. Results use `std::expected`, named request fields, and typed
  errors.
- `include/pawingo/tasks/` owns cancellable worker lifetime without depending on a frontend.
- `include/pawingo/io/` owns shared standard-library temporary artifacts for downloads and imports.
- `plugins/japanese/dictionary.*` parse and query dictionary data without FTXUI or host
  dependencies. `search_panel.*` combines those services with inputs, resource discovery, and status
  presentation.
- `plugin.hpp` is the native/UI extension boundary. `core.hpp` remains a compatibility facade;
  implementation files should include the header owned by their layer.

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

The xmake `embed_voicevox` hook generates `build/generated/pawingo_voicevox_assets.hpp` for the
Japanese plugin and installer tests. Changed resource contents rewrite the generated header, causing
normal compiler dependency tracking to rebuild its consumers. The generated header is a build
artifact; maintain the YAML and text sources instead. Installed plugins do not read project files.

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

The shared xmake `embed_assets` mechanism embeds these sources in generated headers. The
`pawingo-config` static library provides an immutable frontend-neutral `Settings` API; YAML types
remain private to its implementation. Host defaults and Japanese defaults have separate ownership.
Resource changes trigger recompilation; installed binaries do not need the source tree or YAML files
at runtime. Editing bundled defaults requires rebuilding. User-editable voice paths and style
selection use the per-user YAML preference file and do not require rebuilding.

The audit intentionally preserves executable contracts in their native formats: C API names and
plugin ABI symbols, database/ORM field names and independent SQL migrations, XML vocabulary and its
specified defaults, Unicode encoding rules, mathematical unit conversions, keyboard event handling,
small structural whitespace/spacing operations, and validation ceilings. These are implementation
invariants rather than product defaults. Error diagnostics remain beside the code that raises them.
Tests retain explicit fixtures and expected values so they can detect accidental changes in bundled
behavior. Build dependencies and their versions remain declarative in xmake and the Nix lock file;
YAML is not used as an additional build-system language.
