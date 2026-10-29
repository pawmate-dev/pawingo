module;
#include <yaml-cpp/yaml.h>

module pawingo.japanese.manifest;

import std;
import pawingo.japanese.assets;

namespace pawingo::japanese {
namespace {
bool safe_name(const std::string &name) {
  return !name.empty() && name != "." && name != ".." &&
    name.find_first_of("/\\:") == std::string::npos;
}

std::string resolve(std::string value, const YAML::Node &variables) {
  for (const auto &pair : variables) {
    auto key = "{" + pair.first.as<std::string>() + "}";
    auto replacement = pair.second.as<std::string>();
    std::size_t pos = 0;
    while ((pos = value.find(key, pos)) != std::string::npos) {
      value.replace(pos, key.size(), replacement);
      pos += replacement.size();
    }
  }
  if (value.find_first_of("{}") != std::string::npos)
    throw std::runtime_error("Unresolved bundle variable.");
  return value;
}
} // namespace

std::expected<VoicevoxManifest, VoicevoxError> parse_voicevox_manifest(std::string_view yaml,
  const std::string &platform) {
  try {
    auto doc = YAML::Load(std::string(yaml));
    if (doc["schema"].as<int>() != 1)
      throw std::runtime_error("Unsupported voice bundle schema.");
    auto variables = doc["platforms"][platform];
    if (!variables.IsMap())
      throw std::runtime_error("Automatic installation is unavailable for " + platform +
        "; configure resources manually.");
    VoicevoxManifest m;
    m.bundle = doc["bundle"].as<std::string>();
    if (!safe_name(m.bundle))
      throw std::runtime_error("Unsafe bundle identifier.");
    m.download_bytes = doc["limits"]["download_bytes"].as<std::size_t>();
    m.timeout = std::chrono::seconds(doc["limits"]["timeout_seconds"].as<unsigned>());
    if (!m.download_bytes || m.download_bytes > 1024ULL * 1024 * 1024 || m.timeout.count() == 0 ||
      m.timeout.count() > 3600)
      throw std::runtime_error("Invalid bundle limits.");
    auto discovery = doc["discovery"];
    m.core_names = discovery["core"].as<std::vector<std::string>>();
    m.runtime_names = discovery["runtime"].as<std::vector<std::string>>();
    m.dictionary_marker = discovery["dictionary"].as<std::string>();
    m.model_name = discovery["model"].as<std::string>();
    if (m.core_names.empty() || m.runtime_names.empty() || !safe_name(m.dictionary_marker) ||
      !safe_name(m.model_name))
      throw std::runtime_error("Invalid resource discovery rules.");
    for (auto &name : m.core_names)
      if (!safe_name(name))
        throw std::runtime_error("Invalid library discovery name.");
    for (auto &name : m.runtime_names)
      if (!safe_name(name) || std::count(name.begin(), name.end(), '*') > 1)
        throw std::runtime_error("Invalid runtime discovery pattern.");
    auto resources = doc["resources"];
    if (!resources.IsSequence() || resources.size() == 0 || resources.size() > 32)
      throw std::runtime_error("Invalid resource list.");
    std::set<std::string> names;
    for (auto node : resources) {
      VoicevoxResource r{node["name"].as<std::string>(),
        resolve(node["url"].as<std::string>(), variables),
        resolve(node["sha256"].as<std::string>(""), variables),
        node["unpack"].as<bool>()};
      if (!safe_name(r.name) || !names.insert(r.name).second)
        throw std::runtime_error("Unsafe or duplicate resource name.");
      if (!r.url.starts_with("https://"))
        throw std::runtime_error("Bundle resources must use HTTPS.");
      if (!r.sha256.empty() &&
        (r.sha256.size() != 64 ||
          r.sha256.find_first_not_of("0123456789abcdef") != std::string::npos))
        throw std::runtime_error("Invalid resource checksum.");
      m.resources.push_back(std::move(r));
    }
    return m;
  } catch (const std::exception &e) {
    return std::unexpected(VoicevoxError{VoicevoxErrorKind::configuration, e.what()});
  }
}

std::expected<VoicevoxManifest, VoicevoxError>
bundled_voicevox_manifest(const std::string &platform) {
  return parse_voicevox_manifest(assets::voicevox_manifest, platform);
}

std::string_view voicevox_runtime_terms() {
  return assets::voicevox_runtime_terms;
}
} // namespace pawingo::japanese
