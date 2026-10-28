module;
#include <curl/curl.h>
#include <picosha2.h>

module pawingo.net;

import std;

namespace pawingo::net {
namespace {
/// Internal exception bridge used to unwind C++ setup and file ownership safely.
struct Failure : std::runtime_error {
  DownloadErrorKind kind;
  long status = 0;

  explicit Failure(DownloadErrorKind category, const std::string &message)
    : std::runtime_error(message),
      kind(category) {
  }
};

struct CurlRuntime {
  CurlRuntime() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
      throw Failure(DownloadErrorKind::initialization, "Cannot initialize HTTPS client.");
  }

  ~CurlRuntime() {
    curl_global_cleanup();
  }
};

struct Transfer {
  std::ofstream output;
  std::size_t limit, received = 0;
  std::stop_token stop;
  picosha2::hash256_one_by_one hash;
  DownloadErrorKind failure = DownloadErrorKind::connection;
};

std::size_t write_data(char *bytes, std::size_t size, std::size_t count, void *data) noexcept {
  auto &transfer = *static_cast<Transfer *>(data);
  if (size && count > std::numeric_limits<std::size_t>::max() / size) {
    transfer.failure = DownloadErrorKind::response_too_large;
    return 0;
  }
  const auto length = size * count;
  if (transfer.stop.stop_requested())
    return 0;
  if (length > transfer.limit - transfer.received) {
    transfer.failure = DownloadErrorKind::response_too_large;
    return 0;
  }
  try {
    transfer.output.write(bytes, static_cast<std::streamsize>(length));
    if (!transfer.output) {
      transfer.failure = DownloadErrorKind::file_write;
      return 0;
    }
    transfer.hash.process(bytes, bytes + length);
    transfer.received += length;
    return length;
  } catch (...) {
    transfer.failure = DownloadErrorKind::file_write;
    return 0;
  }
}

int progress(void *data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
  return static_cast<Transfer *>(data)->stop.stop_requested() ? 1 : 0;
}

template <class Value> void option(CURL *curl, CURLoption name, Value value) {
  const auto code = curl_easy_setopt(curl, name, value);
  if (code != CURLE_OK)
    throw Failure(DownloadErrorKind::initialization, curl_easy_strerror(code));
}
} // namespace

namespace {
DownloadReceipt perform(const DownloadRequest &request, std::stop_token stop) {
  const auto &url = request.url;
  const auto &destination = request.destination;
  const auto max_bytes = request.max_bytes;
  const auto timeout_seconds = request.timeout.count();
  const auto &expected_sha256 = request.expected_sha256;
  if (!url.starts_with("https://"))
    throw Failure(DownloadErrorKind::invalid_request, "An HTTPS URL is required.");
  if (!max_bytes || timeout_seconds <= 0 || timeout_seconds > std::numeric_limits<long>::max())
    throw Failure(DownloadErrorKind::invalid_request, "Invalid download limits.");
  if (!expected_sha256.empty() &&
    (expected_sha256.size() != 64 ||
      expected_sha256.find_first_not_of("0123456789abcdef") != std::string::npos))
    throw Failure(DownloadErrorKind::invalid_request,
      "Expected SHA-256 must be lowercase hexadecimal.");
  if (stop.stop_requested())
    throw Failure(DownloadErrorKind::cancelled, "Cancelled.");
  static CurlRuntime runtime;
  auto curl =
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), curl_easy_cleanup);
  if (!curl)
    throw Failure(DownloadErrorKind::initialization, "Cannot create HTTPS request.");
  Transfer transfer{std::ofstream(destination, std::ios::binary | std::ios::trunc),
    max_bytes,
    0,
    stop,
    {}};
  if (!transfer.output)
    throw Failure(DownloadErrorKind::file_write, "Cannot create download file.");
  try {
    option(curl.get(), CURLOPT_URL, url.c_str());
    option(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    option(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    option(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    option(curl.get(),
      CURLOPT_MAXREDIRS,
      static_cast<long>(config::application().integer("network.max_redirects", 0, 20)));
    option(curl.get(), CURLOPT_FAILONERROR, 1L);
    option(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    option(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    option(curl.get(), CURLOPT_NOSIGNAL, 1L);
    option(curl.get(),
      CURLOPT_CONNECTTIMEOUT,
      static_cast<long>(config::application().integer("network.connect_timeout_seconds", 1, 300)));
    option(curl.get(), CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    option(curl.get(), CURLOPT_WRITEFUNCTION, write_data);
    option(curl.get(), CURLOPT_WRITEDATA, &transfer);
    option(curl.get(), CURLOPT_NOPROGRESS, 0L);
    option(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    option(curl.get(), CURLOPT_XFERINFODATA, &transfer);
    const auto result = curl_easy_perform(curl.get());
    if (stop.stop_requested())
      throw Failure(DownloadErrorKind::cancelled, "Cancelled.");
    if (result != CURLE_OK) {
      const auto kind = result == CURLE_OPERATION_TIMEDOUT ? DownloadErrorKind::timeout
        : result == CURLE_HTTP_RETURNED_ERROR              ? DownloadErrorKind::http_status
                                                           : transfer.failure;
      Failure failure(kind, std::string("HTTPS download failed: ") + curl_easy_strerror(result));
      curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &failure.status);
      throw failure;
    }
    transfer.output.close();
    if (!transfer.output)
      throw Failure(DownloadErrorKind::file_write, "Cannot finish writing download.");
    transfer.hash.finish();
    if (!expected_sha256.empty() && picosha2::get_hash_hex_string(transfer.hash) != expected_sha256)
      throw Failure(DownloadErrorKind::checksum, "Download checksum failed.");
    return {destination, transfer.received, picosha2::get_hash_hex_string(transfer.hash)};
  } catch (...) {
    transfer.output.close();
    std::error_code error;
    std::filesystem::remove(destination, error);
    throw;
  }
}
} // namespace

DownloadResult download_https(const DownloadRequest &request, std::stop_token stop) {
  try {
    return perform(request, stop);
  } catch (const Failure &error) {
    return std::unexpected(DownloadError{error.kind, error.what(), error.status});
  }
}
} // namespace pawingo::net
