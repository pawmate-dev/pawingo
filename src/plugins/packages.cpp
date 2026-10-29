module;
#include <archive.h>
#include <archive_entry.h>
#include <picosha2.h>
#include <yaml-cpp/yaml.h>
#ifdef _WIN32
#include <windows.h>
#endif

module pawingo.packages;

import std;

namespace pawingo::packages {
namespace {
struct Failure {
  Error error;
};

[[noreturn]] void fail(ErrorKind kind, std::string text) {
  throw Failure{{kind, std::move(text)}};
}

void check_stop(std::stop_token stop) {
  if (stop.stop_requested())
    fail(ErrorKind::cancelled, "Operation cancelled.");
}

template <class F> auto guarded(F work) -> std::expected<decltype(work()), Error> {
  try {
    if constexpr (std::is_void_v<decltype(work())>) {
      work();
      return {};
    } else
      return work();
  } catch (const Failure &error) {
    return std::unexpected(error.error);
  } catch (const YAML::Exception &error) {
    return std::unexpected(Error{ErrorKind::manifest, error.what()});
  } catch (const std::exception &error) {
    return std::unexpected(Error{ErrorKind::storage, error.what()});
  }
}

struct Limits {
  int catalog, download, expanded, entries, timeout;
};

const Limits &limits() {
  static const auto value = [] {
    const auto &s = config::application();
    return Limits{s.integer("packages.catalog_bytes", 1, 4194304),
      s.integer("packages.download_bytes", 1, 1073741824),
      s.integer("packages.expanded_bytes", 1, 1073741824),
      s.integer("packages.max_entries", 1, 100000),
      s.integer("packages.timeout_seconds", 1, 3600)};
  }();
  return value;
}

std::string setting(const char *name) {
  return config::application().text(std::string("packages.") + name);
}

bool token(std::string_view s) {
  return !s.empty() && s.size() <= 128 && s != "." && s != ".." &&
    std::ranges::all_of(s, [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.';
    });
}

bool digest(std::string_view s) {
  return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

bool relative(std::string_view s) {
  if (s.empty() || s.size() > 1024 || s.find_first_of("\\:") != std::string_view::npos ||
    s.find('\0') != std::string_view::npos)
    return false;
  const std::filesystem::path p(s);
  if (p.is_absolute() || p.has_root_name())
    return false;
  for (const auto &part : p)
    if (part == ".." || part == "." || part.empty())
      return false;
  return true;
}

std::array<unsigned, 3> version(const std::string &text) {
  std::array<unsigned, 3> result{};
  std::string_view rest(text);
  for (int i = 0; i < 3; ++i) {
    auto end = rest.find('.');
    const auto part = rest.substr(0, end);
    auto [ptr, code] = std::from_chars(part.data(), part.data() + part.size(), result[i]);
    if (part.empty() || code != std::errc{} || ptr != part.data() + part.size() ||
      (i < 2 && end == rest.npos) || (i == 2 && end != rest.npos))
      fail(ErrorKind::manifest, "Versions must contain three numeric components.");
    if (i < 2)
      rest.remove_prefix(end + 1);
  }
  return result;
}

std::string text(const YAML::Node &node, const char *key, bool empty = false) {
  const auto s = node[key].as<std::string>();
  if ((!empty && s.empty()) || s.size() > 8192 || std::ranges::any_of(s, [](unsigned char c) {
        return c < 32 || c == 127;
      }))
    fail(ErrorKind::manifest, "Invalid package text field.");
  return s;
}

std::string read(const std::filesystem::path &file) {
  if (std::filesystem::file_size(file) > static_cast<unsigned>(limits().catalog))
    fail(ErrorKind::manifest, "Metadata exceeds its byte limit.");
  std::ifstream in(file, std::ios::binary);
  if (!in)
    fail(ErrorKind::storage, "Cannot open package metadata.");
  std::string value(std::istreambuf_iterator<char>(in), {});
  if (in.bad())
    fail(ErrorKind::storage, "Cannot read package metadata.");
  return value;
}

std::string hash_file(const std::filesystem::path &path, std::stop_token stop) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    fail(ErrorKind::storage, "Cannot open downloaded package.");
  picosha2::hash256_one_by_one hash;
  std::array<char, 65536> buffer;
  while (input) {
    check_stop(stop);
    input.read(buffer.data(), buffer.size());
    hash.process(buffer.data(), buffer.data() + input.gcount());
  }
  if (input.bad())
    fail(ErrorKind::storage, "Cannot read downloaded package.");
  hash.finish();
  return picosha2::get_hash_hex_string(hash);
}

void replace(const std::filesystem::path &from, const std::filesystem::path &to) {
#ifdef _WIN32
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    fail(ErrorKind::storage, "Cannot commit package metadata.");
#else
  std::filesystem::rename(from, to);
#endif
}

void atomic_write(const std::filesystem::path &path,
  const std::string &value,
  std::stop_token stop) {
  io::TemporaryArtifact temporary(path.parent_path());
  std::ofstream output(temporary.file(), std::ios::binary);
  output << value;
  output.close();
  if (!output)
    fail(ErrorKind::storage, "Cannot write package metadata.");
  check_stop(stop);
  replace(temporary.file(), path);
}

struct Lock {
  std::filesystem::path path;

  explicit Lock(const std::filesystem::path &root)
    : path(root / ".package-lock") {
    std::filesystem::create_directories(root);
    if (!std::filesystem::create_directory(path))
      fail(ErrorKind::busy, "Another package operation is active.");
  }

  ~Lock() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

bool compatible(const Package &p, const Artifact &a) {
  return p.plugin_api == 1 && p.host_version == build::version && a.platform == build::platform &&
    a.abi == build::abi;
}

void require_offer(const Offer &offer) {
  if (!offer.artifact || !compatible(offer.package, *offer.artifact))
    fail(ErrorKind::incompatible, "This plugin was built for a different host or platform.");
  if (!token(offer.package.id) || !digest(offer.artifact->sha256) ||
    !relative(offer.artifact->entry))
    fail(ErrorKind::manifest, "Invalid package offer.");
  if (!offer.artifact->size_bytes ||
    offer.artifact->size_bytes > static_cast<unsigned>(limits().download) ||
    !offer.artifact->url.starts_with("https://"))
    fail(ErrorKind::manifest, "Invalid plugin artifact size or URL.");
  (void)version(offer.package.version);
}

void checked_directory(const std::filesystem::path &root, const std::string &relative_path) {
  auto current = root;
  for (const auto &part : std::filesystem::path(relative_path)) {
    current /= part;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
      fail(ErrorKind::manifest, "Package version directories cannot contain symbolic links.");
  }
}

void verify_manifest(const std::filesystem::path &directory, const Installed &p) {
  for (const auto &path :
    {std::filesystem::path(setting("manifest")), std::filesystem::path(p.entry)}) {
    auto current = directory;
    for (const auto &part : path) {
      current /= part;
      if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
        fail(ErrorKind::manifest, "Installed packages cannot contain symbolic links.");
    }
    if (!std::filesystem::is_regular_file(current))
      fail(ErrorKind::manifest, "Installed package is incomplete.");
  }
  auto node = YAML::Load(read(directory / setting("manifest")));
  if (node["schema"].as<int>() != 1 || node["plugin_api"].as<int>() != 1 ||
    text(node, "id") != p.id || text(node, "name") != p.name ||
    text(node, "language") != p.language || text(node, "version") != p.version ||
    text(node, "host_version") != p.host_version || text(node, "platform") != p.platform ||
    text(node, "abi") != p.abi || text(node, "entry") != p.entry)
    fail(ErrorKind::manifest, "Package metadata does not match the selected catalog artifact.");
}

std::vector<Installed> inventory(const std::filesystem::path &root) {
  const auto file = root / setting("inventory");
  if (!std::filesystem::exists(file))
    return {};
  auto doc = YAML::Load(read(file));
  auto rows = doc["plugins"];
  if (doc["schema"].as<int>() != 1 || !rows.IsSequence() || rows.size() > 1024)
    fail(ErrorKind::manifest, "Invalid installed-plugin inventory.");
  std::set<std::string> ids;
  std::vector<Installed> result;
  for (const auto &node : rows) {
    Installed p{text(node, "id"),
      text(node, "name"),
      text(node, "language"),
      text(node, "version"),
      text(node, "host_version"),
      text(node, "platform"),
      text(node, "abi"),
      text(node, "entry"),
      text(node, "sha256"),
      text(node, "directory")};
    if (!token(p.id) || !digest(p.sha256) || !relative(p.entry) ||
      p.directory != p.id + "/" + p.version + "/" + p.sha256 || !relative(p.directory) ||
      !ids.insert(p.id).second)
      fail(ErrorKind::manifest, "Unsafe or duplicate installed plugin.");
    (void)version(p.version);
    result.push_back(std::move(p));
  }
  return result;
}

std::string inventory_yaml(const std::vector<Installed> &rows) {
  YAML::Emitter e;
  e << YAML::BeginMap << YAML::Key << "schema" << YAML::Value << 1 << YAML::Key << "plugins"
    << YAML::Value << YAML::BeginSeq;
  for (const auto &p : rows)
    e << YAML::BeginMap << YAML::Key << "id" << YAML::Value << p.id << YAML::Key << "name"
      << YAML::Value << p.name << YAML::Key << "language" << YAML::Value << p.language << YAML::Key
      << "version" << YAML::Value << p.version << YAML::Key << "host_version" << YAML::Value
      << p.host_version << YAML::Key << "platform" << YAML::Value << p.platform << YAML::Key
      << "abi" << YAML::Value << p.abi << YAML::Key << "entry" << YAML::Value << p.entry
      << YAML::Key << "sha256" << YAML::Value << p.sha256 << YAML::Key << "directory" << YAML::Value
      << p.directory << YAML::EndMap;
  e << YAML::EndSeq << YAML::EndMap;
  return e.c_str();
}

void extract(const std::filesystem::path &file,
  const std::filesystem::path &output,
  std::stop_token stop) {
  std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(),
    archive_read_free);
  archive_read_support_filter_all(reader.get());
  archive_read_support_format_tar(reader.get());
  archive_read_support_format_zip(reader.get());
  if (archive_read_open_filename(reader.get(), file.string().c_str(), 65536) != ARCHIVE_OK)
    fail(ErrorKind::archive, "Cannot open plugin archive.");
  archive_entry *entry = nullptr;
  std::set<std::string> names;
  std::uint64_t total = 0;
  int code;
  std::filesystem::create_directories(output);
  while ((code = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
    check_stop(stop);
    const char *raw = archive_entry_pathname(entry);
    std::string name = raw ? raw : "";
    while (name.ends_with('/'))
      name.pop_back();
    if (!relative(name) || !names.insert(name).second ||
      names.size() > static_cast<unsigned>(limits().entries) || archive_entry_symlink(entry) ||
      archive_entry_hardlink(entry))
      fail(ErrorKind::archive, "Unsafe path, link or duplicate archive entry.");
    const auto destination = output / name;
    if (archive_entry_filetype(entry) == AE_IFDIR) {
      std::filesystem::create_directories(destination);
      continue;
    }
    if (archive_entry_size(entry) < 0 || archive_entry_size(entry) > limits().expanded ||
      archive_entry_sparse_count(entry) > 0)
      fail(ErrorKind::archive, "Oversized or sparse plugin file.");
    if (archive_entry_filetype(entry) != AE_IFREG)
      fail(ErrorKind::archive, "Unsupported archive entry type.");
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream out(destination, std::ios::binary);
    std::array<char, 65536> buffer;
    la_ssize_t size;
    while ((size = archive_read_data(reader.get(), buffer.data(), buffer.size())) > 0) {
      check_stop(stop);
      total += size;
      if (total > static_cast<unsigned>(limits().expanded))
        fail(ErrorKind::archive, "Plugin archive exceeds its expansion budget.");
      out.write(buffer.data(), size);
      if (!out)
        fail(ErrorKind::storage, "Cannot extract plugin file.");
    }
    if (size < 0)
      fail(ErrorKind::archive, "Corrupt plugin archive.");
    out.close();
    if (!out)
      fail(ErrorKind::storage, "Cannot finish extracted plugin file.");
  }
  if (code != ARCHIVE_EOF)
    fail(ErrorKind::archive, "Truncated plugin archive.");
}

void activate(const std::filesystem::path &root,
  const Offer &offer,
  const std::filesystem::path &file,
  std::stop_token stop) {
  require_offer(offer);
  check_stop(stop);
  const auto &p = offer.package;
  const auto &a = *offer.artifact;
  if (std::filesystem::file_size(file) != a.size_bytes || hash_file(file, stop) != a.sha256)
    fail(ErrorKind::checksum, "Plugin size or SHA-256 did not match the catalog.");
  io::TemporaryArtifact stage(root);
  extract(file, stage.file(), stop);
  Installed record{p.id,
    p.name,
    p.language,
    p.version,
    p.host_version,
    a.platform,
    a.abi,
    a.entry,
    a.sha256,
    p.id + "/" + p.version + "/" + a.sha256};
  verify_manifest(stage.file(), record);
  auto rows = inventory(root);
  std::erase_if(rows, [&](const auto &old) {
    return old.id == record.id;
  });
  rows.push_back(record);
  checked_directory(root, record.directory);
  const auto destination = root / record.directory;
  check_stop(stop);
  if (std::filesystem::exists(destination)) {
    // Never overwrite a mapped library or silently reuse an unrelated directory.
    verify_manifest(destination, record);
    if (hash_file(destination / a.entry, stop) != hash_file(stage.file() / a.entry, stop))
      fail(ErrorKind::checksum, "Existing package contents differ from the verified artifact.");
  } else {
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::rename(stage.file(), destination);
  }
  // An orphaned version is harmless if cancellation occurs here; only the inventory activates it.
  atomic_write(root / setting("inventory"), inventory_yaml(rows), stop);
}
} // namespace

std::expected<std::vector<Package>, Error> parse_catalog(std::string_view yaml) {
  try {
    if (yaml.size() > static_cast<unsigned>(limits().catalog))
      fail(ErrorKind::manifest, "Plugin catalog exceeds its byte limit.");
    const auto doc = YAML::Load(std::string(yaml));
    const auto rows = doc["plugins"];
    if (doc["schema"].as<int>() != 1 || !rows.IsSequence() || rows.size() > 1024)
      fail(ErrorKind::manifest, "Invalid plugin catalog schema.");
    std::vector<Package> result;
    std::set<std::string> versions;
    for (const auto &node : rows) {
      Package p{text(node, "id"),
        text(node, "name"),
        text(node, "description", true),
        text(node, "language"),
        text(node, "version"),
        text(node, "host_version"),
        node["plugin_api"].as<unsigned>(),
        {}};
      if (!token(p.id) || !token(p.language))
        fail(ErrorKind::manifest, "Invalid plugin identity.");
      (void)version(p.version);
      (void)version(p.host_version);
      const auto artifacts = node["artifacts"];
      if (!artifacts.IsMap() || artifacts.size() > 16)
        fail(ErrorKind::manifest, "Invalid plugin platform list.");
      for (const auto &pair : artifacts) {
        const auto &a = pair.second;
        Artifact artifact{pair.first.as<std::string>(),
          text(a, "url"),
          text(a, "sha256"),
          text(a, "entry"),
          text(a, "abi"),
          a["size_bytes"].as<std::uint64_t>()};
        if (!token(artifact.platform) || !artifact.url.starts_with("https://") ||
          artifact.url.size() > 2048 || !digest(artifact.sha256) || !relative(artifact.entry) ||
          !artifact.size_bytes || artifact.size_bytes > static_cast<unsigned>(limits().download) ||
          !versions.insert(p.id + "/" + p.version + "/" + artifact.platform + "/" + artifact.abi)
            .second)
          fail(ErrorKind::manifest, "Unsafe or duplicate plugin artifact.");
        p.artifacts.push_back(std::move(artifact));
      }
      result.push_back(std::move(p));
    }
    return result;
  } catch (const Failure &e) {
    return std::unexpected(e.error);
  } catch (const std::exception &e) {
    return std::unexpected(Error{ErrorKind::manifest, e.what()});
  }
}

std::string build_info(std::string_view bundled) {
  auto catalog = parse_catalog(bundled);
  if (!catalog)
    throw std::runtime_error(catalog.error().message);
  picosha2::hash256_one_by_one hash;
  hash.process(bundled.begin(), bundled.end());
  hash.finish();
  const auto catalog_sha256 = picosha2::get_hash_hex_string(hash);
  YAML::Emitter e;
  e << YAML::Flow << YAML::DoubleQuoted << YAML::BeginMap << YAML::Key << "version" << YAML::Value
    << build::version << YAML::Key << "release_build" << YAML::Value << build::release << YAML::Key
    << "platform" << YAML::Value << build::platform << YAML::Key << "abi" << YAML::Value
    << build::abi << YAML::Key << "catalog_sha256" << YAML::Value << catalog_sha256 << YAML::Key
    << "plugins" << YAML::Value << YAML::BeginSeq;
  for (const auto &p : *catalog)
    e << YAML::BeginMap << YAML::Key << "id" << YAML::Value << p.id << YAML::Key << "name"
      << YAML::Value << p.name << YAML::Key << "description" << YAML::Value << p.description
      << YAML::Key << "language" << YAML::Value << p.language << YAML::Key << "version"
      << YAML::Value << p.version << YAML::EndMap;
  e << YAML::EndSeq << YAML::EndMap;
  return e.c_str();
}

Manager::Manager(std::filesystem::path root, std::string bundled, Transfer transfer)
  : root_(std::move(root)),
    bundled_(std::move(bundled)),
    transfer_(std::move(transfer)) {
  if (!transfer_)
    transfer_ = net::download_https;
}

std::expected<std::vector<Installed>, Error> Manager::installed() const {
  return guarded([&] {
    return inventory(root_);
  });
}

std::expected<std::vector<std::filesystem::path>, Error> Manager::enabled() const {
  return guarded([&] {
    std::vector<std::filesystem::path> result;
    for (const auto &p : inventory(root_)) {
      if (p.host_version != build::version || p.platform != build::platform || p.abi != build::abi)
        continue;
      checked_directory(root_, p.directory);
      verify_manifest(root_ / p.directory, p);
      result.push_back(root_ / p.directory / p.entry);
    }
    return result;
  });
}

std::expected<std::vector<Offer>, Error> Manager::offers() const {
  return guarded([&] {
    std::vector<Package> all;
    if (!bundled_.empty()) {
      auto parsed = parse_catalog(bundled_);
      if (!parsed)
        throw Failure{parsed.error()};
      all = std::move(*parsed);
    }
    const auto cache = root_ / setting("cache");
    if (std::filesystem::is_regular_file(cache)) {
      // A corrupted/offline cache must not hide the catalog embedded in this exact host build.
      try {
        if (auto parsed = parse_catalog(read(cache)))
          all.insert(all.end(), parsed->begin(), parsed->end());
      } catch (...) {
      }
    }
    std::map<std::string, Offer> chosen;
    for (const auto &p : all) {
      Offer candidate{p,
        {},
        p.artifacts.empty() ? OfferState::unpublished : OfferState::incompatible};
      for (const auto &a : p.artifacts)
        if (compatible(p, a)) {
          candidate.artifact = a;
          candidate.state = OfferState::available;
          break;
        }
      auto it = chosen.find(p.id);
      if (it == chosen.end() || (candidate.artifact && !it->second.artifact) ||
        (candidate.artifact.has_value() == it->second.artifact.has_value() &&
          (version(p.version) > version(it->second.package.version) ||
            (version(p.version) == version(it->second.package.version) &&
              it->second.package.artifacts.empty() && !p.artifacts.empty()))))
        chosen.insert_or_assign(p.id, std::move(candidate));
    }
    const auto active = inventory(root_);
    for (const auto &p : active) {
      const bool current =
        p.host_version == build::version && p.platform == build::platform && p.abi == build::abi;
      auto it = chosen.find(p.id);
      if (it == chosen.end()) {
        chosen.emplace(p.id,
          Offer{Package{p.id, p.name, "", p.language, p.version, p.host_version, 1, {}},
            {},
            current ? OfferState::installed : OfferState::incompatible});
      } else if (current &&
        (!it->second.artifact || version(p.version) > version(it->second.package.version))) {
        // An older cached catalog must not offer a downgrade of a newer installed package.
        it->second.package.version = p.version;
        it->second.artifact.reset();
        it->second.state = OfferState::installed;
      } else if (it->second.artifact) {
        it->second.state = current && p.version == it->second.package.version &&
            p.sha256 == it->second.artifact->sha256
          ? OfferState::installed
          : OfferState::update;
      }
    }
    std::vector<Offer> result;
    for (auto &[id, offer] : chosen)
      result.push_back(std::move(offer));
    return result;
  });
}

std::expected<void, Error> Manager::refresh(std::stop_token stop) {
  return guarded([&] {
    Lock lock(root_);
    check_stop(stop);
    io::TemporaryArtifact download(root_);
    auto result = transfer_({.url = setting("catalog_url"),
                              .destination = download.file(),
                              .max_bytes = static_cast<std::size_t>(limits().catalog),
                              .timeout = std::chrono::seconds(limits().timeout)},
      stop);
    if (!result)
      fail(result.error().kind == net::DownloadErrorKind::cancelled ? ErrorKind::cancelled
                                                                    : ErrorKind::network,
        result.error().message);
    const auto bytes = read(download.file());
    auto parsed = parse_catalog(bytes);
    if (!parsed)
      throw Failure{parsed.error()};
    check_stop(stop);
    replace(download.file(), root_ / setting("cache"));
  });
}

std::expected<void, Error> Manager::install(const Offer &offer, std::stop_token stop) {
  return guarded([&] {
    require_offer(offer);
    Lock lock(root_);
    check_stop(stop);
    const auto &a = *offer.artifact;
    for (const auto &active : inventory(root_)) {
      if (active.id == offer.package.id && active.version == offer.package.version &&
        active.sha256 == a.sha256 && active.abi == a.abi) {
        checked_directory(root_, active.directory);
        verify_manifest(root_ / active.directory, active);
        return;
      }
    }
    io::TemporaryArtifact download(root_);
    auto result = transfer_({.url = a.url,
                              .destination = download.file(),
                              .max_bytes = static_cast<std::size_t>(a.size_bytes),
                              .timeout = std::chrono::seconds(limits().timeout),
                              .expected_sha256 = a.sha256},
      stop);
    if (!result)
      fail(result.error().kind == net::DownloadErrorKind::cancelled ? ErrorKind::cancelled
          : result.error().kind == net::DownloadErrorKind::checksum ? ErrorKind::checksum
                                                                    : ErrorKind::network,
        result.error().message);
    activate(root_, offer, download.file(), stop);
  });
}

std::expected<void, Error> Manager::install_archive(const Offer &offer,
  const std::filesystem::path &file,
  std::stop_token stop) {
  return guarded([&] {
    require_offer(offer);
    Lock lock(root_);
    activate(root_, offer, file, stop);
  });
}
} // namespace pawingo::packages
