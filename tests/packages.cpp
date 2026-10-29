#include <archive.h>
#include <archive_entry.h>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <picosha2.h>
#include <yaml-cpp/yaml.h>

import std;
import pawingo.packages_ui;
import pawingo.plugins;
import pawingo.io;

using namespace pawingo;
using namespace pawingo::packages;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

std::string hash(const std::filesystem::path &path) {
  return picosha2::hash256_hex_string(read(path));
}

std::string manifest(const Package &p, const Artifact &a) {
  YAML::Node n;
  n["schema"] = 1;
  n["id"] = p.id;
  n["name"] = p.name;
  n["language"] = p.language;
  n["version"] = p.version;
  n["host_version"] = p.host_version;
  n["plugin_api"] = 1;
  n["platform"] = a.platform;
  n["abi"] = a.abi;
  n["entry"] = a.entry;
  return YAML::Dump(n);
}

std::string catalog(const Offer &offer) {
  const auto &p = offer.package;
  const auto &a = *offer.artifact;
  YAML::Node n;
  n["schema"] = 1;
  YAML::Node row;
  row["id"] = p.id;
  row["name"] = p.name;
  row["description"] = p.description;
  row["language"] = p.language;
  row["version"] = p.version;
  row["host_version"] = p.host_version;
  row["plugin_api"] = 1;
  auto artifact = row["artifacts"][a.platform];
  artifact["url"] = a.url;
  artifact["sha256"] = a.sha256;
  artifact["entry"] = a.entry;
  artifact["abi"] = a.abi;
  artifact["size_bytes"] = a.size_bytes;
  n["plugins"].push_back(row);
  return YAML::Dump(n);
}

void tar(const std::filesystem::path &file,
  const std::vector<std::pair<std::string, std::string>> &files,
  bool link = false) {
  auto *writer = archive_write_new();
  archive_write_set_format_pax_restricted(writer);
  archive_write_add_filter_gzip(writer);
  require(archive_write_open_filename(writer, file.string().c_str()) == ARCHIVE_OK,
    "create package fixture");
  for (const auto &[name, bytes] : files) {
    auto *entry = archive_entry_new();
    archive_entry_set_pathname(entry, name.c_str());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    if (link) {
      archive_entry_set_filetype(entry, AE_IFLNK);
      archive_entry_set_symlink(entry, "/tmp");
      archive_entry_set_size(entry, 0);
    } else
      archive_entry_set_size(entry, bytes.size());
    require(archive_write_header(writer, entry) == ARCHIVE_OK, "write archive header");
    if (!link)
      require(archive_write_data(writer, bytes.data(), bytes.size()) ==
          static_cast<la_ssize_t>(bytes.size()),
        "write archive data");
    archive_entry_free(entry);
  }
  archive_write_close(writer);
  archive_write_free(writer);
}

Offer fixture(const std::filesystem::path &file,
  const std::filesystem::path &library,
  std::string version = "0.1.0",
  bool wrong = false) {
  Offer offer{{"fixture",
                "Fixture",
                "Offline test plugin",
                "fixture",
                std::move(version),
                build::version,
                1,
                {}},
    Artifact{build::platform,
      "https://example.test/plugin.tar.gz",
      "",
      "lib/libfixture.so",
      build::abi,
      0},
    OfferState::available};
  auto meta = offer.package;
  if (wrong)
    meta.id = "other";
  tar(file,
    {{"plugin.yaml", manifest(meta, *offer.artifact)}, {"lib/libfixture.so", read(library)}});
  offer.artifact->size_bytes = std::filesystem::file_size(file);
  offer.artifact->sha256 = hash(file);
  offer.package.artifacts = {*offer.artifact};
  return offer;
}

template <class F> void await(F predicate) {
  for (int i = 0; i < 2000; ++i) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("package worker timed out");
}

int main(int argc, char **argv) {
  try {
    require(argc == 2, "native fixture library required");
    io::TemporaryArtifact temporary(std::filesystem::temp_directory_path());
    const auto root = temporary.file();
    std::filesystem::create_directories(root);
    const auto file = root / "plugin.tar.gz";
    auto offer = fixture(file, argv[1]);
    auto yaml = catalog(offer);
    auto parsed = parse_catalog(yaml);
    require(parsed && parsed->size() == 1 && parsed->front().id == "fixture",
      "parse platform-specific checksummed catalog");
    auto invalid = offer;
    invalid.artifact->entry = "../escape";
    auto rejected = parse_catalog(catalog(invalid));
    require(!rejected && rejected.error().kind == ErrorKind::manifest,
      "reject catalog path traversal");
    invalid = offer;
    invalid.artifact->url = "http://example.test/plugin";
    require(!parse_catalog(catalog(invalid)), "reject non-HTTPS artifacts");
    invalid = offer;
    invalid.artifact->sha256 = "";
    require(!parse_catalog(catalog(invalid)), "require artifact checksum");
    {
      auto stub = YAML::Load(yaml);
      stub["plugins"][0]["artifacts"] = YAML::Node(YAML::NodeType::Map);
      auto published = offer;
      published.artifact->abi += "-other-sdk";
      std::filesystem::create_directories(root / "published");
      std::ofstream(root / "published/catalog-v1.yaml") << catalog(published);
      Manager published_manager(root / "published", YAML::Dump(stub));
      require(
        published_manager.offers()->front().state == OfferState::incompatible,
        "published incompatible metadata takes precedence over an equal-version offline placeholder"
      );
    }
    std::atomic<int> transfers = 0;
    std::string response = yaml;
    Transfer transfer = [&](const net::DownloadRequest &request,
                          std::stop_token stop) -> net::DownloadResult {
      ++transfers;
      if (stop.stop_requested())
        return std::unexpected(net::DownloadError{net::DownloadErrorKind::cancelled, "cancelled"});
      if (request.expected_sha256.empty())
        std::ofstream(request.destination) << response;
      else {
        require(request.expected_sha256 == offer.artifact->sha256,
          "pin package checksum in transport");
        std::filesystem::copy_file(file, request.destination);
      }
      return net::DownloadReceipt{request.destination,
        static_cast<std::size_t>(std::filesystem::file_size(request.destination)),
        hash(request.destination)};
    };
    auto manager = std::make_shared<Manager>(root / "plugins", yaml, transfer);
    require(manager->offers()->front().state == OfferState::available && transfers == 0,
      "offline listing never touches the network");
    Store store(":memory:");
    auto panel = ui::package_panel(manager, store);
    panel.component->OnEvent(ftxui::Event::Return);
    require(panel.help().find("always") != std::string::npos && transfers == 0,
      "default install requires explicit service consent");
    require(panel.component->OnEvent(ftxui::Event::Escape) && transfers == 0,
      "Escape cancels consent before downloading");
    panel.component->OnEvent(ftxui::Event::Character('r'));
    panel.component->OnEvent(ftxui::Event::Character('a'));
    await([&] {
      return panel.help().find("r refresh") != std::string::npos;
    });
    require(store.policy(config::application().text("packages.service")) == OnlinePolicy::allow &&
        transfers == 1,
      "always consent persists only on the UI thread");
    require(!panel.component->OnEvent(ftxui::Event::Escape),
      "list delegates Escape back to the host");
    {
      std::atomic<bool> entered = false;
      auto slow = std::make_shared<Manager>(root / "slow",
        yaml,
        [&](const net::DownloadRequest &, std::stop_token stop) -> net::DownloadResult {
          entered = true;
          while (!stop.stop_requested())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          return std::unexpected(net::DownloadError{net::DownloadErrorKind::cancelled,
            "cancelled"});
        });
      auto busy_panel = ui::package_panel(slow, store);
      busy_panel.component->OnEvent(ftxui::Event::Character('r'));
      await([&] {
        return entered.load();
      });
      const auto began = std::chrono::steady_clock::now();
      busy_panel.component->OnEvent(ftxui::Event::ArrowDown);
      require(busy_panel.component->OnEvent(ftxui::Event::Escape) &&
          std::chrono::steady_clock::now() - began < std::chrono::milliseconds(100),
        "Escape requests cancellation without blocking terminal input");
      await([&] {
        return !busy_panel.component->OnEvent(ftxui::Event::Escape);
      });
      require(!std::filesystem::exists(root / "slow/.package-lock"),
        "cancelled worker releases its package lock before returning to the list");
    }
    const auto good_cache = read(root / "plugins/catalog-v1.yaml");
    response = "not: a catalog";
    auto bad_refresh = manager->refresh();
    require(!bad_refresh && bad_refresh.error().kind == ErrorKind::manifest &&
        read(root / "plugins/catalog-v1.yaml") == good_cache,
      "failed refresh preserves the valid cache");
    std::ofstream(root / "plugins/catalog-v1.yaml") << "broken";
    require(manager->offers()->front().state == OfferState::available,
      "bad cache falls back to the embedded catalog");
    require(bool(manager->install_archive(offer, file)),
      "install verified local artifact atomically");
    auto enabled = manager->enabled();
    require(enabled && enabled->size() == 1 && std::filesystem::is_regular_file(enabled->front()),
      "inventory discovers only validated active entry");
    Plugins plugins;
    plugins.discover((root / "plugins").string());
    require(plugins.errors().empty() && plugins.languages().size() == 1 &&
        plugins.languages().front()->language() == "fixture",
      "next-start registry loads a version-directory package");
    const int before = transfers.load();
    require(bool(manager->install(offer)) && transfers == before,
      "already installed artifact is reused without network");
    require(manager->offers()->front().state == OfferState::installed,
      "catalog identifies installed version");
    const auto old_entry = enabled->front();
    const auto update_file = root / "update.tar.gz";
    auto update = fixture(update_file, argv[1], "0.1.1");
    invalid = update;
    invalid.artifact->abi += "-different";
    auto mismatch = manager->install_archive(invalid, update_file);
    require(!mismatch && mismatch.error().kind == ErrorKind::incompatible,
      "reject ABI mismatch before extraction");
    auto corrupt = root / "corrupt";
    std::ofstream(corrupt) << "invalid";
    auto checksum = manager->install_archive(update, corrupt);
    require(!checksum && checksum.error().kind == ErrorKind::checksum,
      "reject corrupted downloaded bytes");
    std::stop_source stop;
    stop.request_stop();
    auto cancelled = manager->install_archive(update, update_file, stop.get_token());
    require(!cancelled && cancelled.error().kind == ErrorKind::cancelled &&
        manager->installed()->front().version == "0.1.0",
      "cancelled update preserves active version");
    const auto wrong_file = root / "wrong.tar.gz";
    auto wrong = fixture(wrong_file, argv[1], "0.1.1", true);
    auto bad_meta = manager->install_archive(wrong, wrong_file);
    require(!bad_meta && bad_meta.error().kind == ErrorKind::manifest,
      "artifact metadata must agree with its catalog");
    const auto evil_file = root / "evil.tar.gz";
    tar(evil_file, {{"../escape", "outside"}});
    auto evil = update;
    evil.artifact->sha256 = hash(evil_file);
    evil.artifact->size_bytes = std::filesystem::file_size(evil_file);
    auto traversal = manager->install_archive(evil, evil_file);
    require(!traversal && traversal.error().kind == ErrorKind::archive &&
        !std::filesystem::exists(root / "escape"),
      "reject archive traversal even with matching checksum");
    tar(evil_file, {{"lib/link", ""}}, true);
    evil.artifact->sha256 = hash(evil_file);
    evil.artifact->size_bytes = std::filesystem::file_size(evil_file);
    auto link = manager->install_archive(evil, evil_file);
    require(!link && link.error().kind == ErrorKind::archive, "reject archive symlinks");
    require(manager->installed()->front().version == "0.1.0",
      "failed updates never replace active inventory");
    require(bool(manager->install_archive(update, update_file)) &&
        manager->installed()->front().version == "0.1.1" && std::filesystem::exists(old_entry),
      "update commits the new version without overwriting the loaded old library");
    require(manager->offers()->front().state == OfferState::installed &&
        manager->offers()->front().package.version == "0.1.1",
      "an older embedded or cached catalog never proposes a downgrade");
    std::filesystem::create_directory(root / "plugins/.package-lock");
    auto busy = manager->refresh();
    require(!busy && busy.error().kind == ErrorKind::busy,
      "serialize package writers across processes");
    std::filesystem::remove(root / "plugins/.package-lock");
    response = yaml;
    std::stop_source during;
    Manager cancelled_manager(root / "plugins",
      yaml,
      [&](const net::DownloadRequest &request, std::stop_token) -> net::DownloadResult {
        std::filesystem::copy_file(file, request.destination);
        during.request_stop();
        return net::DownloadReceipt{request.destination,
          static_cast<std::size_t>(std::filesystem::file_size(file)),
          hash(file)};
      });
    auto late = cancelled_manager.install(offer, during.get_token());
    require(!late && late.error().kind == ErrorKind::cancelled &&
        manager->installed()->front().version == "0.1.1",
      "cancellation after transfer cannot activate an older package");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
