export module pawingo.japanese.manifest;

import std;
export import pawingo.japanese.voicevox;

export namespace pawingo::japanese {
/// One verified download or archive in an ordered offline installation plan.
struct VoicevoxResource {
  std::string name, url, sha256;
  bool unpack;
};

/// Validated, platform-resolved configuration; YAML never escapes this service boundary.
struct VoicevoxManifest {
  std::string bundle;
  std::size_t download_bytes;
  std::chrono::seconds timeout;
  std::vector<VoicevoxResource> resources;
  std::vector<std::string> core_names, runtime_names;
  std::string dictionary_marker, model_name;
};

/// Parses schema 1 and substitutes a supported platform's variables. Rejects unsafe local names,
/// duplicate resources, unresolved variables, non-HTTPS URLs, and invalid checksums or limits.
std::expected<VoicevoxManifest, VoicevoxError> parse_voicevox_manifest(std::string_view yaml,
  const std::string &platform);
/// Returns the build-time embedded installation plan, independent of the working directory.
std::expected<VoicevoxManifest, VoicevoxError>
bundled_voicevox_manifest(const std::string &platform);
/// Returns the matching runtime's embedded upstream terms, preserved verbatim.
std::string_view voicevox_runtime_terms();
} // namespace pawingo::japanese
