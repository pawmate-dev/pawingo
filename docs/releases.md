# Releases and plugin packages

Pawingo publishes the host and Japanese plugin as separate assets of the same release. The release
matrix targets Linux, Windows and macOS, each on x86_64 and ARM64. Both assets must come from one
SDK build; a version range alone does not make native C++ plugins compatible.

For 0.1.0, public binary publication remains blocked on the corresponding-source distribution and
hosted CI verification. See the [0.1.0 changelog](../CHANGELOG.md#010-unreleased). Local test
archives and a draft are preparation artifacts, not evidence that those publication requirements are
complete.

## Building a release

From a clean checkout with xmake, Rust, just and Python 3.12+; Linux additionally needs patchelf,
and macOS needs the Xcode command-line tools. Install FTXUI and other project libraries through
xmake, not through the runner distribution package manager:

```sh
export PAWINGO_RELEASE_BUILD="$(git rev-parse HEAD)"
just release v0.1.0
```

The tag must match the host version in `xmake.lua`. `PAWINGO_RELEASE_BUILD` is a build-time input;
keep it unchanged for both phases. It is not an environment override of compatibility at runtime.
The default identity is `development`, which packaging refuses unless `--allow-development` is
explicitly supplied for a local test.

`just release` builds/tests first, packages Japanese, calculates its final size/SHA-256 and writes
`build/release/plugins-v1.yaml`. Only the host is then rebuilt with `PAWINGO_CATALOG_SOURCE`
pointing at that file. Finally it packages the host, checks that the actual embedded catalog has the
expected hash, runs the relocated host and loads the packaged native plugin as a smoke test.

The output contains:

```text
build/release/
  pawingo-0.1.0-linux-x86_64-gnu.tar.gz
  pawingo-japanese-0.1.0-linux-x86_64-gnu.tar.gz
  plugins-v1.yaml
  SHA256SUMS
```

The plugin archive contains `plugin.yaml`, its entry in `lib/`, bundled native dependencies and
notices. The host archive contains its executable in `bin/`, native dependencies, build metadata and
notices. Run the host as `bin/pawingo` after extraction. The packager rewrites RPATHs to paths
relative to each file; it copies dependency SONAMEs as regular files so packages need no symlinks.
Dictionaries, sentence corpora, VOICEVOX runtimes and voice models remain independent user-managed
resources.

The native matrix uses Ubuntu 24.04 (x86_64/ARM64), Windows 2025 (x86_64), Windows 11 ARM64, macOS
15 Intel and macOS 15 Apple Silicon. Ubuntu uses LLVM 22 with matching libc++; Windows uses MSVC and
its standard-library modules. macOS Intel uses the runner's available LLVM 18, while Apple Silicon
uses Homebrew LLVM 22. These compiler/standard-library combinations still require successful hosted
validation; listing a target does not certify a working release.

Linux packages retain glibc and the ELF loader as system requirements. The packager records the
build's glibc version, bundles the remaining ELF dependency closure and rewrites RPATHs. macOS
packages bundle non-system Mach-O dependencies, rewrite loader-relative references and ad-hoc sign
the modified images. Ad-hoc signing is not Apple Developer ID signing or notarization. Windows
packages bundle architecture-matching imported and delay-loaded DLL dependencies beside their owning
executable or plugin; operating-system DLLs remain system requirements. All platforms use `.tar.gz`,
supported by the shared libarchive installer. Windows runs `bin/pawingo.exe`.

The Nix flake remains a development shell; exporting a Nix closure is a separate distribution path.
Supplied third-party notices and available distro/source notices are copied into each archive;
Pawingo-owned source is MIT licensed. The combined binaries include sqlite_orm under AGPL-3.0; see
the root NOTICE and the [0.1.0 changelog](../CHANGELOG.md#010-unreleased) before making the draft
public.

## GitHub workflow

`.github/workflows/release.yaml` builds natively on six hosted runners when a version tag is pushed;
no Arch container or pacman step is used. A manual dispatch on a branch accepts a version tag as
input and validates/packages the full matrix without creating a Release; dispatching a version tag
can create its draft. Each matrix job uses the commit SHA as its SDK identity, runs the same tests
and Python style checks, packages its plugin, embeds the platform-local catalog in its host and
smoke-tests the relocated host/plugin pair. Native target filenames come from `xmake release-paths`,
so build directory, architecture and `.so`/`.dylib`/`.dll` suffixes are not hardcoded in CI or test
recipes.

Each job uploads its pair, local catalog and checksums as an Actions artifact. Only after every job
succeeds does one aggregation job verify every checksum, the release commit, host identity and
catalog-pinned plugin, merge all six catalog artifacts into `plugins-v1.yaml`, and create one draft
Release. The downloaded catalog contains every platform; the host's embedded offline catalog only
needs its own platform. The final `SHA256SUMS` covers twelve archives and the merged catalog.
`python -m tools.release_collect` rejects missing/duplicate platforms and differing build commits.
The workflow does not publish the draft automatically or overwrite an existing release.

Review the draft assets and notes, then publish through GitHub. When enabling immutable releases,
attach every asset before publication. See
[GitHub release management](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
and [the GitHub CLI release command](https://cli.github.com/manual/gh_release_create).

The workflow is defined locally; it must be exercised on GitHub before treating its runner-specific
build and upload steps as verified. Local packaging does not create a tag, push code or upload
files.

## Catalog contract

`resources/plugins.yaml` is the maintained offline fallback. Its Japanese entry has no binary
artifact until release packaging supplies real hashes; the UI displays it as not yet published.
`PAWINGO_CATALOG_SOURCE` replaces the host's embedded catalog during a release build. Generated JSON
serialization in `.yaml` files is valid YAML and is read by the same yaml-cpp parser.

```yaml
schema: 1
plugins:
  - id: japanese
    name: 日本語
    description: Japanese vocabulary, kana, typed review and optional local speech.
    language: ja
    version: 0.1.0
    host_version: 0.1.0
    plugin_api: 1
    artifacts:
      linux-x86_64-gnu:
        url: https://github.com/pawmate-dev/pawingo/releases/download/v0.1.0/pawingo-japanese-0.1.0-linux-x86_64-gnu.tar.gz
        sha256: "<generated 64-character lowercase SHA-256>"
        size_bytes: 12345678
        entry: lib/libjapanese.so
        abi: "<exact compiled ABI tag>"
```

The example checksum/ABI are placeholders, not installable metadata. The packager generates these
values from actual artifacts. The compatibility tag includes compiler, standard-library release,
libstdc++ string ABI, FTXUI version, module contract epoch, host version, build identity, mode and
platform. Release builders must also keep build flags/dependencies consistent. Rebuild older plugin
binaries against this SDK; neither header-era nor earlier module-era binaries are accepted.

Plugin-local `plugin.yaml` repeats schema, identity, version, host version, plugin API, platform,
ABI and entry. The installer verifies these against the selected catalog artifact without executing
the library. The native descriptor is checked again when the next process loads it. Native plugins
remain trusted code: SHA-256 verifies agreement with the catalog, not an independent publisher
signature. The initial catalog trust source is the configured official GitHub repository over HTTPS.

Opening **Get plugins** reads the cache and embedded metadata only. `r` explicitly requests a
refresh from the configured release catalog URL. A corrupt/unreadable cache falls back to the
embedded catalog. The merge retains embedded compatible entries when the latest remote catalog only
describes a newer host. Highest compatible numeric `major.minor.patch` versions are preferred.
Prerelease version syntax is not currently supported.

The list distinguishes available, installed, update available, incompatible build and not published
yet. Enter installs a compatible entry; it does not load code into the running host. The
`github.plugins` service defaults to asking. Enter permits this operation once; `a` remembers allow
for future explicit requests. Offline policy blocks transfers. Escape consumes a consent prompt or
cancels an active worker before returning home. Merely entering the page never downloads anything.

## Installation and recovery

Packages are kept under the user's configured Pawingo plugin directory:

```text
plugins/
  catalog-v1.yaml
  installed.yaml
  japanese/
    0.1.0/
      <full artifact SHA-256>/
        plugin.yaml
        lib/libjapanese.so
```

The full checksum distinguishes immutable builds even if a package version was reused. An
installation downloads to scratch space, verifies exact length/SHA-256, extracts with entry and
expanded-byte limits, rejects traversal, links and special files, verifies metadata, then renames
the staged version into place. Atomic inventory replacement is the activation commit point. Failed
or cancelled writes preserve the previous inventory; a leftover unreferenced version is harmless.
This guarantees atomic visibility, not power-loss durability on every filesystem.

The next startup validates active metadata/ABI before loading only the registered entry; bundled
dependency libraries are not discovered as plugins. Existing flat development `.so`/`.dylib`/`.dll`
discovery and `PAWINGO_PLUGIN_DIR` continue to work. Changes require a restart. An already installed
identical artifact is reused without another transfer. Old version directories are kept;
uninstall/garbage collection and in-process hot reload are not implemented.

Writers use a `.package-lock` directory. A normal exit removes it; after a crash, remove a stale
lock only after ensuring no package operation is running. No automatic stale-lock recovery is
attempted. User learning data and external dictionaries/models are never placed inside a package
version directory.

Offline installation uses an explicitly supplied catalog and archive:

```sh
pawingo --install-plugin japanese ./pawingo-japanese-0.1.0-linux-x86_64-gnu.tar.gz ./plugins-v1.yaml
```

The command runs the same compatibility, checksum, extraction and activation path, with no network.
Useful inspection commands are:

```sh
pawingo --build-info
pawingo --check-plugin /path/to/libjapanese.so
```

`--check-plugin` executes a trusted library's initializer/descriptor and creates its plugin
instance; it is a packaging smoke test, not a sandbox or a pre-install authenticity check.
