#pragma once
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

/// Standard-library ownership for temporary filesystem artifacts.
namespace pawingo::io {
/// Owns a unique scratch directory and one candidate artifact path.
class TemporaryArtifact {
public:
  /// Creates scratch storage below parent without opening the candidate artifact.
  ///
  /// @param parent Directory on the same filesystem as the eventual installation target.
  /// @throws std::filesystem::filesystem_error For directory creation failures.
  /// @throws std::runtime_error If a unique directory cannot be allocated.
  ///
  /// The caller can validate the candidate and rename it out of this directory to commit.
  /// Destruction attempts to remove remaining contents without throwing.
  explicit TemporaryArtifact(const std::filesystem::path &parent) {
    std::filesystem::create_directories(parent);
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
      auto candidate =
        parent / (".artifact-" + std::to_string(random()) + "-" + std::to_string(random()));
      if (std::filesystem::create_directory(candidate)) {
        directory_ = std::move(candidate);
        return;
      }
    }
    throw std::runtime_error("Cannot create temporary artifact directory.");
  }

  ~TemporaryArtifact() {
    std::error_code error;
    std::filesystem::remove_all(directory_, error);
  }

  TemporaryArtifact(const TemporaryArtifact &) = delete;
  TemporaryArtifact &operator=(const TemporaryArtifact &) = delete;

  /// Returns the candidate file path owned by this scratch directory.
  std::filesystem::path file() const {
    return directory_ / "artifact";
  }

private:
  std::filesystem::path directory_;
};
} // namespace pawingo::io
