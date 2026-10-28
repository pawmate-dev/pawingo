#include "../plugins/japanese/voicevox_install.hpp"
#include "../plugins/japanese/voicevox_manifest.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <fstream>
#include <iostream>
using namespace pawingo::japanese;

void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

void make_archive(const std::filesystem::path &file, const char *name) {
  auto a = archive_write_new();
  archive_write_set_format_pax_restricted(a);
  archive_write_open_filename(a, file.string().c_str());
  auto e = archive_entry_new();
  archive_entry_set_pathname(e, name);
  archive_entry_set_filetype(e, AE_IFREG);
  archive_entry_set_perm(e, 0644);
  archive_entry_set_size(e, 3);
  archive_write_header(a, e);
  archive_write_data(a, "abc", 3);
  archive_entry_free(e);
  archive_write_close(a);
  archive_write_free(a);
}

int main(int argc, char **argv) {
  try {
    for (const auto &platform :
      {"linux-x64", "linux-arm64", "osx-x64", "osx-arm64", "windows-x64"}) {
      auto bundle = bundled_voicevox_manifest(platform);
      require(bundle && bundle->resources.size() == 6,
        "embedded manifest resolves supported platforms");
      require(bundle->resources.front().sha256.size() == 64, "core checksum resolves by platform");
      for (const auto &resource : bundle->resources)
        require(resource.url.find('{') == std::string::npos, "URL variables are resolved");
    }
    require(!bundled_voicevox_manifest("windows-arm64"),
      "unsupported platform fails before network access");
    const std::string fixture = R"yaml(
schema: 1
bundle: test
limits: {download_bytes: 1024, timeout_seconds: 10}
platforms: {test: {arch: x64}}
discovery:
  core: [core.dll]
  runtime: [runtime.dll]
  dictionary: sys.dic
  model: voice.vvm
resources:
  - {name: voice.vvm, url: 'https://example.org/{arch}/model', unpack: false}
)yaml";
    require(bool(parse_voicevox_manifest(fixture, "test")), "valid manifest");
    auto malformed = fixture;
    malformed.replace(malformed.find("schema: 1"), 9, "schema: 2");
    require(!parse_voicevox_manifest(malformed, "test"), "unsupported schema rejected");
    malformed = fixture;
    malformed.replace(malformed.find("https://"), 8, "http://");
    require(!parse_voicevox_manifest(malformed, "test"), "insecure download rejected");
    malformed = fixture;
    malformed.replace(malformed.find("name: voice.vvm"), 15, "name: ../escape");
    require(!parse_voicevox_manifest(malformed, "test"), "unsafe resource filename rejected");
    malformed = fixture;
    malformed.replace(malformed.find("{arch}/model"), 6, "{missing}");
    require(!parse_voicevox_manifest(malformed, "test"), "unresolved variable rejected");
    pawingo::io::TemporaryArtifact temp(std::filesystem::temp_directory_path());
    auto root = temp.file().parent_path();
    make_archive(temp.file(), "nested/test.txt");
    extract_voicevox_archive(temp.file(), root / "safe", {});
    require(std::filesystem::file_size(root / "safe/nested/test.txt") == 3,
      "regular file extracted");
    make_archive(temp.file(), "../escaped.txt");
    bool rejected = false;
    try {
      extract_voicevox_archive(temp.file(), root / "unsafe", {});
    } catch (...) {
      rejected = true;
    }
    require(rejected && !std::filesystem::exists(root / "escaped.txt"), "reject traversal");
    std::stop_source stop;
    stop.request_stop();
    rejected = false;
    try {
      extract_voicevox_archive(temp.file(), root / "cancelled", stop.get_token());
    } catch (...) {
      rejected = true;
    }
    require(rejected, "cancel extraction");
    if (argc > 1 && std::string(argv[1]) == "--live") {
      auto result = prepare_voicevox({}, [](std::string message) {
        std::cout << message << std::endl;
      });
      require(bool(result), result ? "" : result.error().message.c_str());
      require(!result->terms.empty(), "review terms available");
      std::cout << "Official bundle downloaded and validated; discarding unaccepted candidate."
                << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
