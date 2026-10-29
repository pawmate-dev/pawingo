module;
#include <yaml-cpp/yaml.h>
#define VOICEVOX_LOAD_ONNXRUNTIME
#include "voicevox_core.h"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

module pawingo.japanese.voicevox;

import std;
import pawingo.io;
import pawingo.japanese.settings;

namespace pawingo::japanese {
namespace {
using Json = nlohmann::json;
// A process-wide cache deliberately pins native runtimes, including across plugin instances.

void *library(const std::string &path) {
#ifdef _WIN32
  auto handle = LoadLibraryW(std::filesystem::path(path).c_str());
#else
  auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
  if (!handle)
    throw std::runtime_error("Cannot load VOICEVOX library: " + path);
  return reinterpret_cast<void *>(handle);
}

template <class F> F symbol(void *lib, const char *name) {
#ifdef _WIN32
  auto value = GetProcAddress(static_cast<HMODULE>(lib), name);
#else
  auto value = dlsym(lib, name);
#endif
  if (!value)
    throw std::runtime_error(std::string("Missing VOICEVOX symbol: ") + name);
  return reinterpret_cast<F>(value);
}

struct Api {
  void *lib;
#define VV(name) decltype(&voicevox_##name) name = symbol<decltype(name)>(lib, "voicevox_" #name)
  VV(onnxruntime_load_once);
  VV(open_jtalk_rc_new);
  VV(open_jtalk_rc_delete);
  VV(synthesizer_new);
  VV(synthesizer_delete);
  VV(synthesizer_load_voice_model);
  VV(voice_model_file_open);
  VV(voice_model_file_delete);
  VV(voice_model_file_create_metas_json);
  VV(synthesizer_create_audio_query);
  VV(synthesizer_synthesis);
  VV(wav_free);
  VV(json_free);
  VV(error_result_to_message);
#undef VV

  void check(VoicevoxResultCode result) {
    if (result != VOICEVOX_RESULT_OK)
      throw std::runtime_error(error_result_to_message(result));
  }
};

VoicevoxAccelerationMode acceleration_mode() {
  const auto mode = settings().text("tts.acceleration");
  if (mode == "cpu")
    return VOICEVOX_ACCELERATION_MODE_CPU;
  if (mode == "auto")
    return VOICEVOX_ACCELERATION_MODE_AUTO;
  if (mode == "gpu")
    return VOICEVOX_ACCELERATION_MODE_GPU;
  throw std::runtime_error("Unknown VOICEVOX acceleration mode.");
}

VoicevoxError failure(const std::exception &e) {
  return {VoicevoxErrorKind::runtime, e.what()};
}

// Fold only ordinary kana, without guessing that different long-vowel spellings are equivalent.
std::string kana_key(const std::string &text) {
  std::string result;
  for (std::size_t i = 0; i < text.size();) {
    if (i + 2 < text.size() && static_cast<unsigned char>(text[i]) == 0xe3 &&
      (static_cast<unsigned char>(text[i + 1]) & 0xc0) == 0x80 &&
      (static_cast<unsigned char>(text[i + 2]) & 0xc0) == 0x80) {
      unsigned cp = ((text[i] & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
      if (cp >= 0x30a1 && cp <= 0x30f6) {
        cp -= 0x60;
        result += static_cast<char>(0xe0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (cp & 0x3f));
        i += 3;
        continue;
      }
    }
    result += text[i++];
  }
  return result;
}

bool matches_reading(const char *query, const std::string &reading) {
  auto document = Json::parse(query);
  std::string pronunciation;
  for (const auto &phrase : document.at("accent_phrases")) {
    // A pause is not part of a single dictionary reading; do not silently discard it.
    if (phrase.contains("pause_mora") && !phrase.at("pause_mora").is_null())
      return false;
    for (const auto &mora : phrase.at("moras"))
      pronunciation += mora.at("text").get<std::string>();
  }
  return !pronunciation.empty() && kana_key(pronunciation) == kana_key(reading);
}
} // namespace

struct Voicevox::Impl {
  std::mutex mutex;
  std::unique_ptr<Api> api;
  std::string core, key;
  VoicevoxSynthesizer *synth = nullptr;

  ~Impl() {
    reset();
  }

  void reset() {
    if (synth)
      api->synthesizer_delete(synth);
    synth = nullptr;
    key.clear();
  }

  Api &get(const VoicevoxConfig &c) {
    if (c.core.empty() || !std::filesystem::is_regular_file(c.core))
      throw std::runtime_error("Choose a VOICEVOX CORE library file.");
    if (core != c.core) {
      reset();
      api = std::make_unique<Api>(library(c.core));
      core = c.core;
    }
    return *api;
  }
};

Voicevox::Voicevox()
  : impl_(std::make_unique<Impl>()) {
}

Voicevox::~Voicevox() = default;

std::filesystem::path Voicevox::config_path() {
  return config::preferences_path(settings().text("tts.preferences"));
}

std::expected<VoicevoxConfig, VoicevoxError> Voicevox::load_config() {
  try {
    auto path = config_path();
    if (!std::filesystem::exists(path))
      path = config::preferences_path(settings().text("tts.legacy_preferences"));
    const auto default_style =
      static_cast<std::uint32_t>(settings().integer("tts.default_style", 0, 100000));
    if (!std::filesystem::exists(path)) {
      VoicevoxConfig value;
      value.style = default_style;
      return value;
    }
    auto j = YAML::LoadFile(path.string());
    return VoicevoxConfig{j["core"].as<std::string>(""),
      j["runtime"].as<std::string>(""),
      j["dictionary"].as<std::string>(""),
      j["model"].as<std::string>(""),
      j["style"].as<std::uint32_t>(default_style)};
  } catch (const std::exception &e) {
    return std::unexpected(VoicevoxError{VoicevoxErrorKind::configuration, e.what()});
  }
}

std::expected<void, VoicevoxError> Voicevox::save_config(const VoicevoxConfig &c) {
  try {
    auto path = config_path();
    std::filesystem::create_directories(path.parent_path());
    pawingo::io::TemporaryArtifact tmp(path.parent_path());
    {
      std::ofstream out(tmp.file());
      YAML::Node document;
      document["schema"] = 1;
      document["core"] = c.core;
      document["runtime"] = c.runtime;
      document["dictionary"] = c.dictionary;
      document["model"] = c.model;
      document["style"] = c.style;
      out << YAML::Dump(document);
      out.close();
      if (!out)
        throw std::runtime_error("Cannot save voice configuration.");
    }
#ifdef _WIN32
    if (!MoveFileExW(tmp.file().c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
      throw std::runtime_error("Cannot replace voice configuration.");
#else
    std::filesystem::rename(tmp.file(), path);
#endif
    return {};
  } catch (const std::exception &e) {
    return std::unexpected(VoicevoxError{VoicevoxErrorKind::configuration, e.what()});
  }
}

std::expected<std::vector<VoicevoxStyle>, VoicevoxError> Voicevox::styles(const VoicevoxConfig &c) {
  std::lock_guard lock(impl_->mutex);
  try {
    auto &a = impl_->get(c);
    VoicevoxVoiceModelFile *raw = nullptr;
    a.check(a.voice_model_file_open(c.model.c_str(), &raw));
    std::unique_ptr<VoicevoxVoiceModelFile, decltype(a.voice_model_file_delete)> model(raw,
      a.voice_model_file_delete);
    std::unique_ptr<char, decltype(a.json_free)> text(a.voice_model_file_create_metas_json(raw),
      a.json_free);
    auto meta = Json::parse(text.get());
    std::vector<VoicevoxStyle> result;
    for (const auto &speaker : meta)
      for (const auto &s : speaker.at("styles"))
        if (s.value("type", "talk") == "talk")
          result.push_back({s.at("id").get<std::uint32_t>(),
            speaker.at("name").get<std::string>() + " · " + s.at("name").get<std::string>()});
    return result;
  } catch (const std::exception &e) {
    return std::unexpected(failure(e));
  }
}

std::expected<void, VoicevoxError>
Voicevox::speak(const VoicevoxConfig &c, const std::string &text, std::stop_token stop) {
  return speak(c, SpeechRequest{.text = text}, stop);
}

std::expected<void, VoicevoxError>
Voicevox::speak(const VoicevoxConfig &c, const SpeechRequest &request, std::stop_token stop) {
  std::lock_guard lock(impl_->mutex);
  auto cancelled = [&] {
    return std::unexpected(VoicevoxError{VoicevoxErrorKind::cancelled, "Speech cancelled."});
  };
  try {
    if (stop.stop_requested())
      return cancelled();
    auto &a = impl_->get(c);
    auto key = Json::array({c.core, c.runtime, c.dictionary, c.model}).dump();
    if (impl_->key != key) {
      impl_->reset();
      const VoicevoxOnnxruntime *runtime = nullptr;
      a.check(a.onnxruntime_load_once({c.runtime.c_str()}, &runtime));
      OpenJtalkRc *raw = nullptr;
      a.check(a.open_jtalk_rc_new(c.dictionary.c_str(), &raw));
      std::unique_ptr<OpenJtalkRc, decltype(a.open_jtalk_rc_delete)> dict(raw,
        a.open_jtalk_rc_delete);
      a.check(a.synthesizer_new(runtime,
        raw,
        {acceleration_mode(), static_cast<uint16_t>(settings().integer("tts.cpu_threads", 0, 256))},
        &impl_->synth));
      VoicevoxVoiceModelFile *m = nullptr;
      a.check(a.voice_model_file_open(c.model.c_str(), &m));
      std::unique_ptr<VoicevoxVoiceModelFile, decltype(a.voice_model_file_delete)> model(m,
        a.voice_model_file_delete);
      a.check(a.synthesizer_load_voice_model(impl_->synth, m));
      impl_->key = key;
    }
    if (stop.stop_requested())
      return cancelled();
    auto analyze = [&](const std::string &text) {
      char *raw = nullptr;
      a.check(a.synthesizer_create_audio_query(impl_->synth, text.c_str(), c.style, &raw));
      return std::unique_ptr<char, decltype(a.json_free)>(raw, a.json_free);
    };
    const auto &text = request.text.empty() ? request.reading : request.text;
    auto query = analyze(text);
    if (stop.stop_requested())
      return cancelled();
    if (!request.reading.empty() && !matches_reading(query.get(), request.reading)) {
      query = analyze(request.reading);
      if (stop.stop_requested())
        return cancelled();
    }
    uintptr_t length = 0;
    uint8_t *bytes = nullptr;
    // Pass the original query through, preserving phrase accents and all predicted mora pitches.
    a.check(a.synthesizer_synthesis(impl_->synth,
      query.get(),
      c.style,
      {settings().flag("tts.interrogative_upspeak")},
      &length,
      &bytes));
    std::unique_ptr<uint8_t, decltype(a.wav_free)> wav(bytes, a.wav_free);
    if (stop.stop_requested())
      return cancelled();
    ma_decoder decoder;
    if (ma_decoder_init_memory(bytes, length, nullptr, &decoder) != MA_SUCCESS)
      return std::unexpected(VoicevoxError{VoicevoxErrorKind::audio,
        "Cannot decode speech audio."});

    struct DecoderGuard {
      ma_decoder *p;

      ~DecoderGuard() {
        ma_decoder_uninit(p);
      }
    } dg{&decoder};

    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = decoder.outputFormat;
    config.playback.channels = decoder.outputChannels;
    config.sampleRate = decoder.outputSampleRate;
    config.pUserData = &decoder;
    config.dataCallback = [](ma_device *d, void *out, const void *, ma_uint32 frames) {
      ma_decoder_read_pcm_frames(static_cast<ma_decoder *>(d->pUserData), out, frames, nullptr);
    };
    ma_device device;
    if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS)
      return std::unexpected(VoicevoxError{VoicevoxErrorKind::audio, "Cannot open audio device."});

    struct DeviceGuard {
      ma_device *p;

      ~DeviceGuard() {
        ma_device_uninit(p);
      }
    } guard{&device};

    if (ma_device_start(&device) != MA_SUCCESS)
      return std::unexpected(VoicevoxError{VoicevoxErrorKind::audio,
        "Cannot start audio playback."});
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
    auto end = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(frames * 1000 / decoder.outputSampleRate +
        settings().integer("tts.playback_tail_ms", 0, 5000));
    while (!stop.stop_requested() && std::chrono::steady_clock::now() < end)
      std::this_thread::sleep_for(
        std::chrono::milliseconds(settings().integer("tts.playback_poll_ms", 1, 1000))
      );
    if (stop.stop_requested())
      return cancelled();
    return {};
  } catch (const std::exception &e) {
    return std::unexpected(failure(e));
  }
}
} // namespace pawingo::japanese
