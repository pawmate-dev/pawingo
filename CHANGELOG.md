# Changelog

## 0.1.0 (unreleased)

Public host/plugin binary publication is **not ready yet**. The local functional baseline and
packaging can be evaluated as a release candidate, but an unpublished draft or a prerelease label
does not replace the distribution requirements of incorporated dependencies.

### Version and license decisions

- Host version: `0.1.0`; Japanese plugin version: `0.1.0`.
- Initial target: Linux x86_64 with GNU libc; older distributions, Windows and macOS are not
  claimed.
- Pawingo-owned source: MIT, recorded in the root `LICENSE`.
- Combined binaries: distributed under the AGPL conditions of sqlite_orm v1.9.1, with the owner's
  approval. `NOTICE` explains the distinction; this is not an MIT-only binary release.
- Retained direct C++ notices include FTXUI, sqlite_orm, miniaudio, Expat, nlohmann/json and
  PicoSHA2. Existing FSRS and VOICEVOX header notices remain in the package as well.

Upstream references:

- [sqlite_orm v1.9.1 license](https://github.com/fnc12/sqlite_orm/blob/v1.9.1/LICENSE)
- [AGPL version 3, including corresponding-source distribution](https://www.gnu.org/licenses/agpl-3.0.html)
- [MIT license template](https://opensource.org/license/mit)

### Local verification

The normal `just build` and `just test` passed, including module ownership checks and Rust/C++
tests. All four Python packaging regression tests and Ruff checks passed. Candidate archive
checksums and mandatory license files were verified. Relocated-host navigation, consent/Escape,
offline plugin installation, restart loading and installed status passed at both 80 x 24 and 120 x
40 terminal sizes. Candidate artifacts use the explicit `--allow-development` flag in an isolated
output directory; they must not be uploaded as production builds. Local artifacts are under
`build/release/0.1.0-candidate/`. The ordinary development build was restored after these checks.

### Remaining publication blockers

1. Prepare and review the complete corresponding-source distribution, including incorporated
   third-party preferred source, applicable transitive notices and the build/install materials
   required for the combined binary distribution. The current packager copies notices and
   dependencies but does not produce this source distribution. GitHub's automatic archive of Pawingo
   alone must not be assumed to contain all incorporated dependency source.
2. Commit the version/license/preparation changes, then run the hosted release workflow against that
   exact release commit. No hosted run existed at the start of this check; the Arch container,
   GitHub token permissions and upload stage still require a real hosted validation.
3. Review the resulting draft assets and source availability before publication. Keep the compiled
   SDK identity tied to the final release commit; do not reuse local development artifacts.

No tag, push, remote draft or public release is created by this local readiness check.
