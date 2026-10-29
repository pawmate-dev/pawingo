export module pawingo.packages;

import std;
export import pawingo.net;
export import pawingo.build;

/// Catalog parsing, verified native packages and restart-based activation; no terminal
/// dependencies.
export namespace pawingo::packages {
/// Recoverable package failure categories; callers never need to parse diagnostics.
enum class ErrorKind {
  manifest,
  incompatible,
  cancelled,
  network,
  checksum,
  archive,
  storage,
  busy
};

/// Typed failure with an English diagnostic for presentation at the UI boundary.
struct Error {
  ErrorKind kind;
  std::string message;
};

/// One immutable platform artifact; entry is relative to the extracted package root.
struct Artifact {
  std::string platform, url, sha256, entry, abi;
  std::uint64_t size_bytes = 0;
};

/// Catalog metadata shared across platform artifacts of one plugin version.
struct Package {
  std::string id, name, description, language, version, host_version;
  unsigned plugin_api = 1;
  std::vector<Artifact> artifacts;
};

/// Active version record. Directory is relative to the manager root and includes the full hash.
struct Installed {
  std::string id, name, language, version, host_version, platform, abi, entry, sha256, directory;
};
/// Installation status relative to this exact running host and its persisted inventory.
enum class OfferState { available, installed, update, incompatible, unpublished };

/// Selected plugin version; incompatible/unpublished entries have no installable artifact.
struct Offer {
  Package package;
  std::optional<Artifact> artifact;
  OfferState state;
};

/// Parses bounded schema-1 metadata; paths, identifiers, checksums and HTTPS URLs are validated.
std::expected<std::vector<Package>, Error> parse_catalog(std::string_view yaml);
/// Returns machine-readable JSON (also YAML) for release tooling; no network or plugin loading.
std::string build_info(std::string_view bundled_catalog);

using Transfer = std::function<net::DownloadResult(const net::DownloadRequest &, std::stop_token)>;

/// A package store. Read operations are offline; refresh/install require explicit caller consent.
/// Writers acquire a cross-process directory lock. Atomic inventory replacement is the activation
/// commit point; cancelled/failed work preserves active versions. A committed install is not loaded
/// into this process; the registry activates it at the next startup. Old versions remain available.
class Manager {
public:
  /// Borrows no host services. The bundled catalog is copied; transfer defaults to library HTTPS.
  explicit Manager(std::filesystem::path root,
    std::string bundled_catalog = {},
    Transfer transfer = {});
  /// Merges valid cached metadata with the bundled fallback, preferring compatible versions.
  /// Invalid remote caches fall back to bundled metadata; never fetches the network.
  std::expected<std::vector<Offer>, Error> offers() const;
  /// Reads the inventory offline; malformed metadata and filesystem failures return typed errors.
  std::expected<std::vector<Installed>, Error> installed() const;
  /// Validates active package metadata before returning native library paths. Incompatible SDK
  /// records remain on disk and are skipped; malformed current-build records fail the operation.
  std::expected<std::vector<std::filesystem::path>, Error> enabled() const;
  /// Downloads and validates a catalog before replacing the cache. Stops before the commit point.
  std::expected<void, Error> refresh(std::stop_token = {});
  /// Downloads the selected compatible artifact, verifies size/SHA-256, extracts without links or
  /// traversal, validates package metadata, then atomically records activation. Never dlopens it.
  std::expected<void, Error> install(const Offer &, std::stop_token = {});
  /// Same verification/activation path for a previously downloaded artifact. Source is not changed.
  std::expected<void, Error>
  install_archive(const Offer &, const std::filesystem::path &, std::stop_token = {});

private:
  std::filesystem::path root_;
  std::string bundled_;
  Transfer transfer_;
};
} // namespace pawingo::packages
