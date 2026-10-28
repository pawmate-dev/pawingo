export module pawingo.japanese.voicevox;

import std;

export namespace pawingo::japanese {
/// User-owned offline resources; paths point to files supplied by the official downloader.
struct VoicevoxConfig {
  std::string core, runtime, dictionary, model;
  std::uint32_t style = 0;
};

/// A model's selectable speaker style, identified by its stable upstream ID.
struct VoicevoxStyle {
  std::uint32_t id;
  std::string name;
};
enum class VoicevoxErrorKind { configuration, runtime, audio, cancelled };

struct VoicevoxError {
  VoicevoxErrorKind kind;
  std::string message;
};

/// Serialized native synthesis service shared by all Japanese learning panels.
/// Core inference cannot be interrupted; cancellation is checked between native calls and during
/// playback. Destruction must follow joining every caller. Runtime libraries stay process-resident
/// because ONNX initialization is process-global.
class Voicevox {
public:
  Voicevox();
  ~Voicevox();
  Voicevox(const Voicevox &) = delete;
  Voicevox &operator=(const Voicevox &) = delete;
  static std::filesystem::path config_path();
  static std::expected<VoicevoxConfig, VoicevoxError> load_config();
  static std::expected<void, VoicevoxError> save_config(const VoicevoxConfig &);
  /// Reads style metadata without loading the model into the synthesizer.
  std::expected<std::vector<VoicevoxStyle>, VoicevoxError> styles(const VoicevoxConfig &);
  /// Synthesizes and plays UTF-8 text on the calling worker; no network access occurs.
  std::expected<void, VoicevoxError>
  speak(const VoicevoxConfig &, const std::string &, std::stop_token);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace pawingo::japanese
