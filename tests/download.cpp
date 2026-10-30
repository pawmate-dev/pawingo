import std;
import pawingo.net;

namespace {
using namespace pawingo::net;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void insecure_url_should_not_create_a_destination(const std::filesystem::path &path) {
  const auto result = download_https({.url = "http://example.invalid/", .destination = path});
  require(!result && result.error().kind == DownloadErrorKind::invalid_request,
    "HTTP request must be rejected as invalid");
  require(!std::filesystem::exists(path), "invalid request must not touch destination");
}

void cancelled_request_should_not_access_the_network(const std::filesystem::path &path) {
  std::stop_source stop;
  stop.request_stop();
  const auto result =
    download_https({.url = "https://example.invalid/", .destination = path}, stop.get_token());
  require(!result && result.error().kind == DownloadErrorKind::cancelled,
    "pre-cancelled request must return cancellation");
  require(!std::filesystem::exists(path), "cancelled request must not touch destination");
}

void invalid_checksum_should_be_rejected_before_transfer(const std::filesystem::path &path) {
  const auto result = download_https(
    {.url = "https://example.invalid/", .destination = path, .expected_sha256 = "invalid"}
  );
  require(!result && result.error().kind == DownloadErrorKind::invalid_request,
    "malformed checksum must be invalid_request");
}

void live_transfer_should_report_verified_metadata(const std::filesystem::path &path,
  const std::string &url,
  const std::string &checksum) {
  DownloadRequest request{.url = url,
    .destination = path,
    .max_bytes = 2 * 1024 * 1024,
    .timeout = std::chrono::seconds{60},
    .expected_sha256 = checksum};
  auto result = download_https(request);
  if (!result)
    throw std::runtime_error(result.error().message);
  require(result->bytes == std::filesystem::file_size(path) && result->sha256 == checksum,
    "receipt must describe the verified artifact");
  request.expected_sha256 = std::string(64, '0');
  result = download_https(request);
  require(!result && result.error().kind == DownloadErrorKind::checksum,
    "checksum mismatch must have a distinct error");
  require(!std::filesystem::exists(path), "checksum failure removes partial file");
  request.expected_sha256.clear();
  request.max_bytes = 1;
  result = download_https(request);
  require(!result && result.error().kind == DownloadErrorKind::response_too_large,
    "size limit must have a distinct error");
  require(!std::filesystem::exists(path), "size failure removes partial file");
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::filesystem::path directory;
    {
      TemporaryDownload temporary(std::filesystem::temp_directory_path());
      directory = temporary.file().parent_path();
      require(std::filesystem::is_directory(directory), "scratch directory must exist");
      insecure_url_should_not_create_a_destination(temporary.file());
      cancelled_request_should_not_access_the_network(temporary.file());
      invalid_checksum_should_be_rejected_before_transfer(temporary.file());
      if (argc == 3)
        live_transfer_should_report_verified_metadata(temporary.file(), argv[1], argv[2]);
    }
    require(!std::filesystem::exists(directory), "scratch directory must be released");
  } catch (const std::exception &error) {
    std::cout << error.what() << std::endl;
    return 1;
  }
}
