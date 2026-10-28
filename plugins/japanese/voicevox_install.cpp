module;
#include <archive.h>
#include <archive_entry.h>

module pawingo.japanese.install;

import std;
import pawingo.net;
import pawingo.japanese.settings;
import pawingo.japanese.manifest;

namespace pawingo::japanese {
namespace {
std::filesystem::path root() {
  return config::data_path(settings().text("tts.managed_directory"));
}

std::string platform() {
#if defined(_WIN32)
  const std::string os = "windows";
#elif defined(__APPLE__)
  const std::string os = "osx";
#elif defined(__linux__)
  const std::string os = "linux";
#else
  throw std::runtime_error("Automatic installation is unavailable on this OS.");
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  return os + "-arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  return os + "-x64";
#else
  throw std::runtime_error("Automatic installation requires x64 or arm64.");
#endif
}

const VoicevoxManifest &manifest() {
  static const auto value = [] {
    auto result = bundled_voicevox_manifest(platform());
    if (!result)
      throw std::runtime_error(result.error().message);
    return *result;
  }();
  return value;
}

bool matches(const std::string &name, const std::string &pattern) {
  auto star = pattern.find('*');
  if (star == std::string::npos)
    return name == pattern;
  return name.size() >= pattern.size() - 1 && name.starts_with(pattern.substr(0, star)) &&
    name.ends_with(pattern.substr(star + 1));
}

std::filesystem::path destination() {
  return root() / (manifest().bundle + "-" + platform());
}

VoicevoxConfig discover(const std::filesystem::path &dir) {
  VoicevoxConfig c;
  c.style = static_cast<std::uint32_t>(settings().integer("tts.default_style", 0, 100000));
  for (auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;
    const auto name = entry.path().filename().string();
    const auto path = std::filesystem::absolute(entry.path()).string();
    const auto &rules = manifest();
    if (std::ranges::find(rules.core_names, name) != rules.core_names.end())
      c.core = path;
    if (std::ranges::any_of(rules.runtime_names, [&](const auto &pattern) {
          return matches(name, pattern);
        }))
      c.runtime = path;
    if (name == rules.dictionary_marker)
      c.dictionary = entry.path().parent_path().string();
    if (name == rules.model_name)
      c.model = path;
  }
  if (c.core.empty() || c.runtime.empty() || c.dictionary.empty() || c.model.empty())
    throw std::runtime_error("Incomplete VOICEVOX installation.");
  return c;
}

VoicevoxError error(const std::exception &e) {
  return {VoicevoxErrorKind::configuration, e.what()};
}

void check_stop(std::stop_token stop) {
  if (stop.stop_requested())
    throw std::runtime_error("Installation cancelled.");
}
} // namespace

void extract_voicevox_archive(const std::filesystem::path &file,
  const std::filesystem::path &output,
  std::stop_token stop) {
  std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(),
    archive_read_free);
  archive_read_support_filter_all(reader.get());
  archive_read_support_format_all(reader.get());
  if (archive_read_open_filename(reader.get(), file.string().c_str(), 65536) != ARCHIVE_OK)
    throw std::runtime_error("Cannot open resource archive.");
  archive_entry *entry = nullptr;
  std::uint64_t total = 0;
  unsigned count = 0;
  int code;
  while ((code = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
    check_stop(stop);
    if (++count > static_cast<unsigned>(settings().integer("archive.max_entries", 1, 100000)))
      throw std::runtime_error("Archive has too many entries.");
    auto name = std::string(archive_entry_pathname(entry));
    if (name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
      throw std::runtime_error("Unsafe archive path.");
    std::filesystem::path relative(name);
    if (relative.is_absolute())
      throw std::runtime_error("Absolute archive path.");
    for (auto &part : relative)
      if (part == "..")
        throw std::runtime_error("Archive path traversal.");
    if (archive_entry_symlink(entry) || archive_entry_hardlink(entry)) {
      archive_read_data_skip(reader.get());
      continue;
    }
    auto type = archive_entry_filetype(entry);
    auto target = output / relative;
    if (type == AE_IFDIR) {
      std::filesystem::create_directories(target);
      continue;
    }
    if (type != AE_IFREG)
      throw std::runtime_error("Unsupported archive entry.");
    std::filesystem::create_directories(target.parent_path());
    std::ofstream out(target, std::ios::binary);
    std::array<char, 65536> buffer;
    la_ssize_t size;
    while ((size = archive_read_data(reader.get(), buffer.data(), buffer.size())) > 0) {
      check_stop(stop);
      total += size;
      if (total >
        static_cast<std::uint64_t>(settings().integer("archive.expanded_bytes", 1, 1073741824)))
        throw std::runtime_error("Archive expansion limit exceeded.");
      out.write(buffer.data(), size);
      if (!out)
        throw std::runtime_error("Cannot extract resource.");
    }
    if (size < 0)
      throw std::runtime_error("Corrupt resource archive.");
    out.close();
    if (!out)
      throw std::runtime_error("Cannot finish extracting resource.");
  }
  if (code != ARCHIVE_EOF)
    throw std::runtime_error("Corrupt archive header.");
}

std::expected<VoicevoxConfig, VoicevoxError> installed_voicevox() {
  try {
    return discover(destination());
  } catch (const std::exception &e) {
    return std::unexpected(error(e));
  }
}

std::expected<VoicevoxInstallation, VoicevoxError> prepare_voicevox(std::stop_token stop,
  std::function<void(std::string)> progress) {
  try {
    const auto &plan = manifest();
    VoicevoxInstallation install;
    install.scratch = std::make_unique<io::TemporaryArtifact>(root());
    install.directory = install.scratch->file();
    std::filesystem::create_directory(install.directory);
    for (const auto &resource : plan.resources) {
      check_stop(stop);
      progress(ui_text("download_prefix") + resource.name + ui_text("download_suffix"));
      auto file = install.scratch->file().parent_path() / resource.name;
      net::DownloadRequest request;
      request.url = resource.url;
      request.destination = file;
      request.max_bytes = plan.download_bytes;
      request.timeout = plan.timeout;
      request.expected_sha256 = resource.sha256;
      auto result = net::download_https(request, stop);
      if (!result)
        throw std::runtime_error(result.error().message);
      if (resource.unpack) {
        progress(ui_text("extract_prefix") + resource.name + ui_text("extract_suffix"));
        extract_voicevox_archive(file, install.directory / resource.name, stop);
      } else
        std::filesystem::rename(file, install.directory / resource.name);
    }
    discover(install.directory);
    install.terms = voicevox_runtime_terms();
    const auto markers = settings().strings("archive.terms_markers");
    for (auto &entry : std::filesystem::recursive_directory_iterator(install.directory)) {
      auto name = entry.path().filename().string();
      if (entry.is_regular_file() &&
        std::ranges::any_of(markers,
          [&](const auto &marker) {
            return name.find(marker) != std::string::npos;
          }) &&
        entry.file_size() <
          static_cast<std::uint64_t>(settings().integer("archive.terms_bytes", 1, 1048576))) {
        std::ifstream in(entry.path());
        install.terms += "\n" + entry.path().lexically_relative(install.directory).string() + "\n" +
          std::string(std::istreambuf_iterator<char>(in), {});
      }
    }
    check_stop(stop);
    return install;
  } catch (const std::exception &e) {
    return std::unexpected(VoicevoxError{stop.stop_requested() ? VoicevoxErrorKind::cancelled
                                                               : VoicevoxErrorKind::configuration,
      e.what()});
  }
}

std::expected<VoicevoxConfig, VoicevoxError> commit_voicevox(VoicevoxInstallation &install) {
  try {
    auto target = destination();
    if (!std::filesystem::exists(target)) {
      discover(install.directory);
      std::filesystem::rename(install.directory, target);
    }
    return discover(target);
  } catch (const std::exception &e) {
    return std::unexpected(error(e));
  }
}
} // namespace pawingo::japanese
