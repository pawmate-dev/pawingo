#pragma once
#include "pawingo/io/temporary_artifact.hpp"
#include "voicevox.hpp"
#include <functional>

namespace pawingo::japanese {
/// Staged offline installation. Destroying an unaccepted candidate removes all downloaded files.
struct VoicevoxInstallation {
  std::unique_ptr<io::TemporaryArtifact> scratch;
  std::filesystem::path directory;
  std::string terms;
};

/// Downloads and safely extracts pinned official CPU resources; never loads native code.
/// Caller must show terms and obtain acceptance before committing or loading the installation.
std::expected<VoicevoxInstallation, VoicevoxError> prepare_voicevox(std::stop_token,
  std::function<void(std::string)> progress);
/// Atomically publishes a candidate and discovers resource paths; never overwrites an installation.
std::expected<VoicevoxConfig, VoicevoxError> commit_voicevox(VoicevoxInstallation &);
/// Finds an already installed managed bundle without network access.
std::expected<VoicevoxConfig, VoicevoxError> installed_voicevox();
/// Extracts regular files with traversal, link, file-count, and expanded-size restrictions.
/// Exposed for archive contract tests; output must be a fresh scratch directory.
void extract_voicevox_archive(const std::filesystem::path &,
  const std::filesystem::path &,
  std::stop_token);
} // namespace pawingo::japanese
