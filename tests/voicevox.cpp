#include <ftxui/component/event.hpp>
#include <stdlib.h>
import std;
import pawingo.japanese.voicevox;
import pawingo.japanese.search;
import pawingo.config;
import pawingo.io;

using namespace pawingo::japanese;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

class SearchHost : public pawingo::Host {
public:
  std::filesystem::path dictionary;

  void save(const pawingo::Vocabulary &) override {
  }

  std::vector<pawingo::Resource> resources(pawingo::ResourceKind, const std::string &) override {
    return {{.id = "fixture",
      .name = "fixture",
      .kind = pawingo::ResourceKind::dictionary,
      .language = "ja",
      .location = dictionary.string()}};
  }
};

int main(int argc, char **argv) {
  try {
    pawingo::io::TemporaryArtifact directory(std::filesystem::temp_directory_path());
    auto root = directory.file().parent_path();
#ifdef _WIN32
    _putenv_s("XDG_CONFIG_HOME", root.string().c_str());
    _putenv_s("XDG_DATA_HOME", root.string().c_str());
    _putenv_s("PAWINGO_JMDICT", "");
#else
    setenv("XDG_CONFIG_HOME", root.c_str(), 1);
    setenv("XDG_DATA_HOME", root.c_str(), 1);
    unsetenv("PAWINGO_JMDICT");
#endif
    require(Voicevox::load_config()->core.empty(), "fresh configuration");
    const auto legacy_path = pawingo::config::preferences_path("voicevox.json");
    std::filesystem::create_directories(legacy_path.parent_path());
    {
      std::ofstream legacy(legacy_path);
      legacy << R"({"core":"old-core","style":13})";
    }
    auto legacy = Voicevox::load_config();
    require(legacy && legacy->core == "old-core" && legacy->style == 13,
      "read legacy JSON without losing user preferences");
    VoicevoxConfig config{"/missing/core", "/runtime", "/dictionary", "/model", 42};
    require(bool(Voicevox::save_config(config)), "save configuration");
    auto restored = Voicevox::load_config();
    require(restored && restored->style == 42 && restored->model == config.model,
      "persist paths and stable style ID");
    config.style = 7;
    require(bool(Voicevox::save_config(config)) && Voicevox::load_config()->style == 7,
      "replace configuration");
    require(std::filesystem::exists(legacy_path), "saving YAML preserves legacy configuration");
    require(Voicevox::config_path().extension() == ".yaml", "new preferences use YAML");
    Voicevox service;
    auto styles = service.styles(config);
    require(!styles && styles.error().kind == VoicevoxErrorKind::runtime,
      "missing library returns recoverable error");
    std::stop_source stop;
    stop.request_stop();
    auto result = service.speak(config, "こんにちは", stop.get_token());
    require(!result && result.error().kind == VoicevoxErrorKind::cancelled,
      "cancel before native initialization");
    require(argc == 2, "native runtime fixture path required");
    config.core = argv[1];
    config.model = (root / "speech-trace").string();
    auto trace = [&] {
      std::ifstream input(config.model);
      return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto pronounce = [&](SpeechRequest request) {
      std::ofstream(config.model, std::ios::trunc).close();
      auto spoken = service.speak(config, request, {});
      require(!spoken && spoken.error().kind == VoicevoxErrorKind::audio,
        "fixture reaches synthesis and stops before opening an audio device");
      return trace();
    };
    auto bridge = pronounce({.text = "橋", .reading = "はし"});
    require(bridge.contains("query 橋 style 7") && !bridge.contains("query はし") &&
        bridge.contains(R"("accent":2)") && bridge.contains(R"("pitch":5.4)"),
      "spelling context and predicted pitch survive synthesis");
    auto chopsticks = pronounce({.text = "箸", .reading = "ハシ"});
    require(!chopsticks.contains("load") && !chopsticks.contains("query ハシ") &&
        chopsticks.contains(R"("accent":1)") && chopsticks.contains(R"({"text":"ハ","pitch":5.4})"),
      "homophones retain distinct accents, equivalent kana readings reuse initialized resources");
    auto raw = pronounce({.text = "生", .reading = "なま"});
    require(raw.contains("query 生") && raw.contains("query なま") &&
        raw.contains(R"({"text":"ナ","pitch":5.2})") && !raw.contains(R"("text":"セ")"),
      "alternate selected reading replaces the spelling query before synthesis");
    auto ice = pronounce({.text = "氷", .reading = "こおり"});
    require(ice.contains("query こおり"),
      "different long-vowel spellings conservatively fall back to the selected reading");
    auto kana = pronounce({.text = "あ"});
    require(kana.contains("query あ") && kana.contains("synthesis style 7"),
      "kana and preview speech use predicted accent queries without a reading constraint");

    std::ofstream(config.model, std::ios::trunc).close();
    std::stop_source during_analysis;
    auto pending = std::async(std::launch::async, [&] {
      return service.speak(config, SpeechRequest{.text = "待機"}, during_analysis.get_token());
    });
    bool entered = false;
    for (int i = 0; i < 200 && !entered; ++i) {
      entered = trace().contains("query 待機");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    during_analysis.request_stop();
    std::ofstream(config.model + ".release").close();
    auto stopped = pending.get();
    require(entered && !stopped && stopped.error().kind == VoicevoxErrorKind::cancelled &&
        !trace().contains("synthesis"),
      "cancellation during native analysis prevents subsequent synthesis");

    SearchHost host;
    host.dictionary = root / "dictionary.xml";
    std::ofstream(host.dictionary) << R"(<JMdict><entry><ent_seq>1</ent_seq>
      <k_ele><keb>橋</keb></k_ele><r_ele><reb>はし</reb></r_ele>
      <sense><gloss>bridge</gloss></sense></entry></JMdict>)";
    std::promise<SpeechRequest> received;
    auto spoken_word = received.get_future();
    std::atomic<bool> delivered = false;
    auto panel = search_panel(std::make_shared<Dictionary>(),
      host,
      [&](const SpeechRequest &word, std::stop_token) {
        if (!delivered.exchange(true))
          received.set_value(word);
        return std::string{};
      });
    panel.component->OnEvent(ftxui::Event::Character("橋"));
    panel.component->OnEvent(ftxui::Event::Return);
    for (int i = 0;
      i < 200 && spoken_word.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
      ++i) {
      panel.component->OnEvent(ftxui::Event::CtrlP);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(spoken_word.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready,
      "search pronunciation dispatches on its worker");
    auto selected = spoken_word.get();
    require(selected.text == "橋" && selected.reading == "はし",
      "search speech adapter receives both selected spelling and reading");
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
}
