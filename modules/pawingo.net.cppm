export module pawingo.net;

import std;
export import pawingo.config;
export import pawingo.io;

/// Frontend-neutral HTTPS transfer contracts and temporary-artifact ownership.
export namespace pawingo::net {
/// Failure categories that callers can handle without inspecting message text.
enum class DownloadErrorKind {
  invalid_request, ///< Invalid URL, checksum, or configured transfer limits.
  cancelled, ///< Cooperative cancellation requested by the caller.
  timeout, ///< Connection or overall transfer deadline elapsed.
  connection, ///< Connection, resolver, or TLS transport failed.
  http_status, ///< Remote endpoint returned an unsuccessful HTTP status.
  response_too_large, ///< Streamed body exceeded the configured byte budget.
  file_write, ///< Destination could not be opened, written, or closed.
  checksum, ///< Completed body did not match the expected SHA-256.
  initialization ///< HTTPS runtime or request configuration failed.
};

/// Structured transfer failure; transport-specific details remain diagnostic only.
struct DownloadError {
  DownloadErrorKind kind;
  std::string message;
  /// HTTP response status, or zero when no HTTP response was received.
  long http_status = 0;
};

/// Bounded, idempotent HTTPS GET request; destination should be a scratch path.
struct DownloadRequest {
  std::string url;
  std::filesystem::path destination;
  std::size_t max_bytes = config::application().integer("network.download_bytes", 1, 1073741824);
  std::chrono::seconds timeout{config::application().integer("network.timeout_seconds", 1, 3600)};
  /// Optional lowercase hexadecimal SHA-256 expected for a pinned artifact.
  std::string expected_sha256;
};

/// Completed artifact metadata; a SHA-256 is computed even without a pinned checksum.
struct DownloadReceipt {
  std::filesystem::path path;
  std::size_t bytes;
  std::string sha256;
};

using DownloadResult = std::expected<DownloadReceipt, DownloadError>;

/// Compatibility name for shared temporary-artifact ownership.
using TemporaryDownload = io::TemporaryArtifact;

/// Streams an HTTPS response into a bounded file, validating TLS and optional SHA-256.
///
/// @param request URL, scratch destination, byte budget, timeout, and optional checksum.
/// @param stop Cooperative cancellation token, polled during network transfer.
/// @return Closed artifact metadata, or a structured validation, transport, or file error.
///
/// Redirects remain HTTPS-only. Invalid/pre-cancelled requests do not touch destination.
/// Failures after opening destination remove its partial contents. Callers commit verified
/// scratch files separately so a failed transfer cannot replace an installed resource.
/// This blocking operation belongs on a worker; cancellation latency depends on the resolver.
[[nodiscard]] DownloadResult download_https(const DownloadRequest &request,
  std::stop_token stop = {});
} // namespace pawingo::net
