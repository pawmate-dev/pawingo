#include <zlib.h>

import std;
import pawingo.io;
import pawingo.japanese.examples;

using namespace pawingo::japanese;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

int main(int argc, char **argv) {
  try {
    pawingo::io::TemporaryArtifact scratch(std::filesystem::temp_directory_path());
    const auto root = scratch.file().parent_path();
    Examples corpus(root / "examples.sqlite3");
    ExampleRequest query{{1001, 1, true, true}, "猫", "ネコ", "en"};
    require(corpus.find(query)->empty(), "missing optional corpus does not create a database");
    require(!std::filesystem::exists(root / "examples.sqlite3"),
      "lookup does not install resources");
    const auto input = root / "index.tsv";
    std::ofstream(input) << "1\t101\t猫がいる。\tThere is a cat.\t猫(ねこ)[01]~\n"
                            "2\t102\t別の義項。\tAnother sense.\t猫(ねこ)[02]~\n"
                            "3\t103\t別の読み。\tAnother reading.\t猫(びょう)[01]~\n"
                            "4\t104\t橋がある。\tThere is a bridge.\t橋(はし)[01]~\n"
                            "5\t105\t猫を見た。\tI saw a cat.\t猫[01]\n"
                            "6\t106\t猫が来た。\tThe cat came.\t別表記(#1001)[01]{猫}~\n"
                            "7\t107\t猫は寝る。\tCats sleep.\t猫(ねこ)\n"
                            "8\t108\t別の語。\tA different entry.\t猫(#999)[01]~\n"
                            "9\t109\t猫が好き。\tI like cats.\t猫(ねこ)[01]\n";
    auto installed = corpus.install_index(input);
    if (!installed)
      throw std::runtime_error(installed.error().message);
    require(installed && *installed == 9, "install independent indexed corpus");
    auto found = corpus.find(query);
    require(found && found->size() == 5 && found->front().sentence_id == 1 &&
        (*found)[1].sentence_id == 6 && found->front().checked,
      "entry IDs, readings and senses restrict matches; checked examples rank first");
    require(found->front().translation == "There is a cat." &&
        found->front().translation_id == 101 && found->front().source_url.ends_with("/1") &&
        found->front().translation_url.ends_with("/101") && !found->front().license.empty() &&
        !found->front().attribution.empty(),
      "sentence and translation retain provenance");
    query.entry.unique_headword = false;
    found = corpus.find(query);
    require(found && found->size() == 4 &&
        std::ranges::none_of(*found,
          [](const auto &value) {
            return value.sentence_id == 5;
          }),
      "ambiguous bare headwords are excluded");
    query.entry.unique_reading = false;
    found = corpus.find(query);
    require(found && found->size() == 1 && found->front().sentence_id == 6,
      "homographs sharing a reading require an explicit dictionary entry ID");
    query.entry.unique_headword = query.entry.unique_reading = true;
    query.target = "de";
    found = corpus.find(query);
    require(found && !found->empty() && found->front().translation.empty() &&
        found->front().translation_language == "ger",
      "missing target translations do not fall back to English");
    const auto pairs = root / "de.tsv";
    std::ofstream(pairs) << "\xef\xbb\xbf"
                            "1\t猫がいる。\t201\tEine Katze ist da.\r\n"
                            "99999\t対象外。\t299\tNicht im Index.\r\n";
    auto translated = corpus.install_translations({pairs, "de"});
    require(translated && *translated == 1, "import target sentence pairs by Japanese sentence ID");
    found = corpus.find(query);
    require(found && found->front().translation == "Eine Katze ist da." &&
        found->front().translation_id == 201,
      "imported translation is selected by target language");
    require(bool(corpus.install_index(input)) &&
        corpus.find(query)->front().translation == "Eine Katze ist da.",
      "updating the word index preserves independently imported translations");
    const auto bad = root / "bad.tsv";
    std::ofstream(bad) << "invalid input\n";
    auto rejected = corpus.install_index(bad);
    require(!rejected && rejected.error().kind == ExampleErrorKind::format &&
        corpus.find(query)->front().translation == "Eine Katze ist da.",
      "malformed index leaves the complete installed resource intact");
    std::ofstream(bad) << "1\t誤った文。\t201\tWrong Japanese ID association.\n";
    auto mismatch = corpus.install_translations({bad, "de"});
    require(!mismatch && mismatch.error().kind == ExampleErrorKind::format &&
        corpus.find(query)->front().translation == "Eine Katze ist da.",
      "translation text mismatch cannot overwrite existing translations");
    std::stop_source stop;
    stop.request_stop();
    auto cancelled = corpus.install_index(input, stop.get_token());
    require(!cancelled && cancelled.error().kind == ExampleErrorKind::cancelled,
      "pre-cancelled imports return a typed cancellation without mutation");
    const auto large = root / "large.tsv";
    {
      std::ofstream out(large);
      for (int i = 1; i <= 20000; ++i)
        out << i << '\t' << (i + 100000) << "\t猫がいる。\tThere is a cat.\t猫(ねこ)[01]~\n";
    }
    std::stop_source during;
    auto pending = std::async(std::launch::async, [&] {
      return corpus.install_index(large, during.get_token());
    });
    bool staged = false;
    for (int i = 0; i < 2000 && !staged; ++i) {
      for (const auto &entry : std::filesystem::directory_iterator(root))
        staged |=
          entry.is_directory() && entry.path().filename().string().starts_with(".artifact-");
      if (!staged)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    during.request_stop();
    auto interrupted = pending.get();
    require(staged && !interrupted && interrupted.error().kind == ExampleErrorKind::cancelled &&
        corpus.find(query)->front().translation == "Eine Katze ist da.",
      "cancellation after staging preserves the previous corpus and target translations");
    const auto gzip = root / "index.gz";
    std::ifstream file(input);
    const std::string bytes(std::istreambuf_iterator<char>(file), {});
    auto compressed = gzopen(gzip.c_str(), "wb");
    require(compressed &&
        gzwrite(compressed, bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) &&
        gzclose(compressed) == Z_OK,
      "create gzip corpus fixture");
    require(bool(corpus.install_index(gzip)), "gzip and plain TSV imports share the same parser");
    std::filesystem::resize_file(gzip, std::filesystem::file_size(gzip) - 5);
    auto damaged = corpus.install_index(gzip);
    require(!damaged && damaged.error().kind == ExampleErrorKind::format &&
        !corpus.find(query)->empty(),
      "damaged archive preserves the installed corpus");
    if (argc == 2) {
      auto sample = corpus.install_index(argv[1]);
      require(sample && *sample > 0, "official live format sample imports successfully");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
