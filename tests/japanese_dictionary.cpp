#include <stdlib.h>
#include <unistd.h>
#include <zlib.h>

import std;
import pawingo.japanese.dictionary;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main(int argc, char **argv) {
  try {
    require(argc == 2, "JMdict fixture path required");
    const std::filesystem::path fixture(argv[1]);
    pawingo::japanese::Dictionary dictionary;
    auto result = dictionary.search("猫", "en", {fixture});
    require(result.size() == 2 && result[0].expression == "猫" && result[1].expression == "猫舌",
      "exact match ranks above prefix");
    require(result[0].meaning == "cat; feline" &&
        result[0].source.find("EDRDG") != std::string::npos,
      "gloss and provenance");
    result = dictionary.search("ネコ", "en", {fixture});
    require(result.size() == 2 && result[0].reading == "ねこ", "katakana lookup");
    result = dictionary.search("猫", "de", {fixture});
    require(result.size() == 1 && result[0].meaning == "Katze" &&
        result[0].meaning_language == "ger",
      "target language");
    require(dictionary.search("猫", "zh", {fixture}).empty(), "no silent target fallback");
    require(dictionary.search("CAT", "en", {fixture}).size() == 1,
      "meaning search ignores ASCII case");
    result = dictionary.search("せい", "en", {fixture});
    require(result.size() == 1 && result[0].expression == "性" && result[0].meaning == "nature",
      "reading and sense restrictions");
    result = dictionary.search("はたち", "en", {fixture});
    require(result.size() == 1 && result[0].expression == "はたち", "kana-only restriction");
    require(dictionary.search("%", "en", {fixture}).empty(), "search is literal");
    require(dictionary.search("   ", "en", {fixture}).empty(), "empty query");
    require(dictionary.search("猫", "en-GB", {fixture}).size() == 2, "target locale alias");
    char temporary[] = "/tmp/pawingo-dictionary-test-XXXXXX";
    require(mkdtemp(temporary) != nullptr, "temporary dictionary directory");
    auto directory = std::filesystem::path(temporary);
    const char *old = std::getenv("XDG_DATA_HOME");
    const bool had = old;
    const std::string previous = old ? old : "";
    setenv("XDG_DATA_HOME", directory.c_str(), 1);
    auto gzip = directory / "dictionary.gz";
    std::ifstream input(fixture);
    const std::string xml((std::istreambuf_iterator<char>(input)), {});
    gzFile file = gzopen(gzip.c_str(), "wb");
    require(file != nullptr, "gzip fixture");
    require(gzwrite(file, xml.data(), static_cast<unsigned>(xml.size())) ==
        static_cast<int>(xml.size()),
      "write gzip");
    require(gzclose(file) == Z_OK, "close gzip");
    require(dictionary.install(gzip) > 0, "install gzip dictionary");
    const auto damaged = directory / "damaged.gz";
    std::filesystem::copy_file(gzip, damaged);
    std::filesystem::resize_file(damaged, std::filesystem::file_size(damaged) - 4);
    bool damaged_rejected = false;
    try {
      dictionary.install(damaged);
    } catch (const std::exception &) {
      damaged_rejected = true;
    }
    require(damaged_rejected, "truncated gzip rejected");
    {
      pawingo::japanese::Dictionary reopened;
      require(reopened.search("おはよう", "en", {pawingo::japanese::dictionary_path()}).size() == 1,
        "installed dictionary reused");
    }
    const auto bad = directory / "bad.xml";
    {
      std::ofstream file(bad);
      file << "<JMdict><entry>";
    }
    bool rejected = false;
    try {
      dictionary.install(bad);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "malformed dictionary rejected");
    require(dictionary.search("猫", "en", {pawingo::japanese::dictionary_path()}).size() == 2,
      "failed install preserves dictionary");
    std::stop_source stop;
    stop.request_stop();
    rejected = false;
    try {
      dictionary.install(fixture, stop.get_token());
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "cancel import");
    if (had)
      setenv("XDG_DATA_HOME", previous.c_str(), 1);
    else
      unsetenv("XDG_DATA_HOME");
    std::filesystem::remove_all(directory);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
