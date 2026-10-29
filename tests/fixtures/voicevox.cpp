// Native C API test boundary: deterministic queries, no inference or audio device required.
#define VOICEVOX_LOAD_ONNXRUNTIME
#include "voicevox_core.h"

import std;

struct VoicevoxOnnxruntime {};

struct OpenJtalkRc {};

struct VoicevoxVoiceModelFile {
  std::string trace;
};

struct VoicevoxSynthesizer {
  mutable std::string trace;
};

namespace {
char *copy(const std::string &text) {
  auto *result = new char[text.size() + 1];
  std::copy(text.begin(), text.end(), result);
  result[text.size()] = '\0';
  return result;
}

void record(const VoicevoxSynthesizer *synth, const std::string &text) {
  std::ofstream(synth->trace, std::ios::app) << text << '\n';
}
} // namespace

extern "C" {
VoicevoxResultCode voicevox_onnxruntime_load_once(VoicevoxLoadOnnxruntimeOptions,
  const VoicevoxOnnxruntime **out) {
  static VoicevoxOnnxruntime runtime;
  *out = &runtime;
  return VOICEVOX_RESULT_OK;
}

VoicevoxResultCode voicevox_open_jtalk_rc_new(const char *, OpenJtalkRc **out) {
  *out = new OpenJtalkRc;
  return VOICEVOX_RESULT_OK;
}

void voicevox_open_jtalk_rc_delete(OpenJtalkRc *value) {
  delete value;
}

VoicevoxResultCode voicevox_synthesizer_new(const VoicevoxOnnxruntime *,
  const OpenJtalkRc *,
  VoicevoxInitializeOptions,
  VoicevoxSynthesizer **out) {
  *out = new VoicevoxSynthesizer;
  return VOICEVOX_RESULT_OK;
}

void voicevox_synthesizer_delete(VoicevoxSynthesizer *value) {
  delete value;
}

VoicevoxResultCode voicevox_voice_model_file_open(const char *path, VoicevoxVoiceModelFile **out) {
  *out = new VoicevoxVoiceModelFile{path};
  return VOICEVOX_RESULT_OK;
}

void voicevox_voice_model_file_delete(VoicevoxVoiceModelFile *value) {
  delete value;
}

char *voicevox_voice_model_file_create_metas_json(const VoicevoxVoiceModelFile *) {
  return copy("[]");
}

VoicevoxResultCode voicevox_synthesizer_load_voice_model(const VoicevoxSynthesizer *synth,
  const VoicevoxVoiceModelFile *model) {
  synth->trace = model->trace;
  record(synth, "load");
  return VOICEVOX_RESULT_OK;
}

VoicevoxResultCode voicevox_synthesizer_create_audio_query(const VoicevoxSynthesizer *synth,
  const char *text,
  VoicevoxStyleId style,
  char **out) {
  const std::string word(text);
  record(synth, "query " + word + " style " + std::to_string(style));
  if (word == "待機") {
    while (!std::filesystem::exists(synth->trace + ".release"))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (word == "橋")
    *out = copy(
      R"({"accent_phrases":[{"accent":2,"moras":[{"text":"ハ","pitch":4.2},{"text":"シ","pitch":5.4}]}]})"
    );
  else if (word == "箸")
    *out = copy(
      R"({"accent_phrases":[{"accent":1,"moras":[{"text":"ハ","pitch":5.4},{"text":"シ","pitch":4.2}]}]})"
    );
  else if (word == "生")
    *out = copy(
      R"({"accent_phrases":[{"accent":1,"moras":[{"text":"セ","pitch":5.1},{"text":"イ","pitch":4.3}]}]})"
    );
  else if (word == "なま")
    *out = copy(
      R"({"accent_phrases":[{"accent":1,"moras":[{"text":"ナ","pitch":5.2},{"text":"マ","pitch":4.4}]}]})"
    );
  else if (word == "氷")
    *out = copy(
      R"({"accent_phrases":[{"accent":0,"moras":[{"text":"コ","pitch":4},{"text":"ー","pitch":5},{"text":"リ","pitch":5}]}]})"
    );
  else
    *out = copy(R"({"accent_phrases":[{"accent":0,"moras":[{"text":"ア","pitch":4}]}]})");
  return VOICEVOX_RESULT_OK;
}

VoicevoxResultCode voicevox_synthesizer_synthesis(const VoicevoxSynthesizer *synth,
  const char *query,
  VoicevoxStyleId style,
  VoicevoxSynthesisOptions,
  uintptr_t *length,
  uint8_t **wav) {
  record(synth, "synthesis style " + std::to_string(style) + " " + query);
  // Stop at decoding so tests never open the user's audio device.
  *length = 1;
  *wav = new uint8_t[1]{0};
  return VOICEVOX_RESULT_OK;
}

void voicevox_json_free(char *value) {
  delete[] value;
}

void voicevox_wav_free(uint8_t *value) {
  delete[] value;
}

const char *voicevox_error_result_to_message(VoicevoxResultCode) {
  return "fixture error";
}
}
