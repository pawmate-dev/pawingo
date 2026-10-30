#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <stdlib.h>

import std;
import pawingo.tests.platform;
import pawingo.io;
import pawingo.plugin;
import pawingo.plugins;
import pawingo.ui;
import pawingo.review_ui;

class Host : public pawingo::Host, public pawingo::UiRefresh, public pawingo::ui::ReviewProvider {
public:
  std::vector<pawingo::Vocabulary> saved;
  std::atomic<unsigned> redraws{0};
  pawingo::Store store{":memory:"};
  std::string review_language;

  pawingo::ui::ReviewPanel create_review_panel(const std::string &language,
    pawingo::ui::AnswerMatcher matcher,
    pawingo::ui::ReviewContent content = {}) override {
    review_language = language;
    return pawingo::ui::review_panel(store, language, std::move(matcher), {}, std::move(content));
  }

  void request_redraw() override {
    ++redraws;
  }

  void save(const pawingo::Vocabulary &word) override {
    saved.push_back(word);
  }

  std::vector<pawingo::Resource> resources(pawingo::ResourceKind, const std::string &) override {
    return {};
  }
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main(int argc, char **argv) {
  try {
    require(argc == 4, "plugin and dictionary fixture paths required");
    pawingo::io::TemporaryArtifact isolated(std::filesystem::temp_directory_path());
    const auto isolated_root = isolated.file().parent_path();
    pawingo::tests::environment("XDG_CONFIG_HOME", isolated_root.string());
    pawingo::tests::environment("XDG_DATA_HOME", isolated_root.string());
    pawingo::Plugins registry;
    registry.load(argv[1]);
    auto *plugin = registry.languages().front();
    Host host;
    {
      auto panel = plugin->create_panel("五十音図", host);
      require(bool(panel), "panel");
      auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(40));
      ftxui::Render(screen, panel->Render());
      auto text = screen.ToString();
      require(text.find("あ") != std::string::npos && text.find("ア") != std::string::npos,
        "both scripts");
      require(!panel->OnEvent(ftxui::Event::Character('k')), "removed script key");
      auto *help = dynamic_cast<pawingo::PanelHelpProvider *>(plugin);
      require(help && help->panel_help("五十音図").find("h/k") == std::string::npos,
        "panel footer");
      require(panel->OnEvent(ftxui::Event::Character('d')), "voice state");
      require(panel->OnEvent(ftxui::Event::ArrowDown), "navigation");
    }
    {
      const auto panels = plugin->panels();
      require(std::ranges::find(panels, "復習") != panels.end(),
        "Japanese declares its own review entry");
      host.store.save({"de", "Katze", "", "eng", "cat", "fixture", ""});
      host.store.save({"ja", "猫", "ねこ", "eng", "cat; feline", "fixture", ""});
      auto panel = plugin->create_panel("復習", host);
      auto *help = dynamic_cast<pawingo::PanelHelpProvider *>(plugin);
      require(panel && host.review_language == "ja" && help &&
          help->panel_help("復習").find("check") != std::string::npos,
        "native plugin requests a language-scoped host session and exposes its footer");
      panel->OnEvent(ftxui::Event::Character("feline"));
      panel->OnEvent(ftxui::Event::Return);
      require(panel->OnEvent(ftxui::Event::Escape) && host.store.review_history(2)->empty(),
        "review feedback consumes Escape without grading");
      panel->OnEvent(ftxui::Event::Return);
      panel->OnEvent(ftxui::Event::Character('3'));
      require(host.store.review_history(2)->size() == 1 && host.store.review_history(1)->empty(),
        "plugin grades its Japanese card without touching German favorites");
      require(!panel->OnEvent(ftxui::Event::Escape),
        "completed review delegates Escape to the plugin feature list");
    }
    {
      auto panel = plugin->create_panel("音声設定", host);
      auto focused_label = [&](const std::string &label, const std::string &placeholder) {
        auto screen =
          ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
        ftxui::Render(screen, panel->Render());
        const auto cursor = screen.cursor();
        require(cursor.shape == ftxui::Screen::Cursor::BarBlinking &&
            screen.PixelAt(cursor.x, cursor.y).character == placeholder,
          "focused empty voice setting exposes its placeholder cursor");

        for (int y = 0; y < screen.dimy(); ++y)
          for (int x = 0; x < screen.dimx(); ++x)
            if (screen.PixelAt(x, y).character == label &&
              screen.PixelAt(x, y).foreground_color == pawingo::ui::accent)
              return true;
        return false;
      };
      require(focused_label("c", "c"), "initial core input has an accent focus label");
      panel->OnEvent(ftxui::Event::Tab);
      require(focused_label("r", "V"), "tab moves focus to runtime label");
      panel->OnEvent(ftxui::Event::Tab);
      require(focused_label("d", "O"), "dictionary path cursor");
      panel->OnEvent(ftxui::Event::Tab);
      require(focused_label("m", "v"), "voice model path cursor");
      panel->OnEvent(ftxui::Event::Tab);
      panel->OnEvent(ftxui::Event::Return);
      require(panel->OnEvent(ftxui::Event::Escape),
        "escape cancels download before network access");
      require(!panel->OnEvent(ftxui::Event::Escape), "escape returns after prompt cancellation");
    }
    {
      auto panel = plugin->create_panel("語彙検索", host);
      auto render = [&] {
        auto screen =
          ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
        ftxui::Render(screen,
          ftxui::vbox({panel->Render() | ftxui::center | ftxui::flex, ftxui::text("footer")}));
        return screen;
      };
      auto initial = render();
      auto start = initial.cursor();
      require(start.shape == ftxui::Screen::Cursor::BarBlinking,
        "empty query exposes a visible IME cursor before any input");
      require(initial.PixelAt(start.x, start.y).character == "w",
        "empty query anchors at the first placeholder glyph");
      panel->OnEvent(ftxui::Event::Character("猫"));
      auto typed = render().cursor();
      require(typed.x == start.x + 2 && typed.y == start.y,
        "cursor advances by the committed CJK glyph width");
      panel->OnEvent(ftxui::Event::Backspace);
      auto erased = render().cursor();
      require(erased.x == start.x && erased.y == start.y && erased.shape == start.shape,
        "deleting the last glyph restores the empty input anchor");
      panel->OnEvent(ftxui::Event::Tab);
      auto target = render();
      auto target_cursor = target.cursor();
      require(target_cursor.x > start.x &&
          target.PixelAt(target_cursor.x, target_cursor.y).character == "e",
        "target-language focus owns the cursor");
      panel->OnEvent(ftxui::Event::CtrlD);
      panel->OnEvent(ftxui::Event::ArrowDown);
      panel->OnEvent(ftxui::Event::Return);
      auto file = render();
      auto file_cursor = file.cursor();
      require(file_cursor.shape == start.shape &&
          file.PixelAt(file_cursor.x, file_cursor.y).character == "/",
        "local dictionary import anchors the empty file input");
    }
    pawingo::io::TemporaryArtifact temporary(std::filesystem::temp_directory_path());
    const auto directory = temporary.file().parent_path();
    const std::string original_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    const char *data = std::getenv("XDG_DATA_HOME");
    const bool had_data = data != nullptr;
    const std::string original_data = data ? data : "";
    const auto helper = directory / std::filesystem::path(argv[3]).filename();
    std::filesystem::copy_file(argv[3], helper);
    std::filesystem::permissions(helper,
      std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
        std::filesystem::perms::owner_write);
    pawingo::tests::environment("PATH",
      directory.string() + pawingo::tests::path_separator + original_path);
    pawingo::tests::environment("XDG_DATA_HOME", directory.string());
    {
      auto panel = plugin->create_panel("五十音図", host);
      const auto started = std::chrono::steady_clock::now();
      require(panel->OnEvent(ftxui::Event::Return), "speech request");
      require(panel->OnEvent(ftxui::Event::ArrowDown), "navigation during speech");
      require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(300),
        "speech must not block input");
      auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(40));
      ftxui::Render(screen, panel->Render());
      bool highlighted = false;
      for (int y = 0; y < screen.dimy(); ++y)
        for (int x = 0; x < screen.dimx(); ++x) {
          const auto &pixel = screen.PixelAt(x, y);
          if (pixel.character == "い" && pixel.foreground_color == pawingo::ui::accent)
            highlighted = true;
          require(!pixel.inverted, "kana chart uses color without inverse blocks");
        }
      require(highlighted, "selection updates during speech");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto leaving = std::chrono::steady_clock::now();
      panel.reset();
      require(std::chrono::steady_clock::now() - leaving < std::chrono::milliseconds(500),
        "cancel playback when leaving");
    }
    const auto voice = directory / "pawingo/tts/japanese/mei_normal.htsvoice";
    std::filesystem::create_directories(voice.parent_path());
    {
      std::ofstream file(voice);
      file << "[GLOBAL]\nHTS_VOICE_VERSION:1.0\n";
    }
    std::filesystem::resize_file(voice, 862503);
    for (int reopen = 0; reopen < 2; ++reopen) {
      auto panel = plugin->create_panel("五十音図", host);
      panel->OnEvent(ftxui::Event::Character('d'));
      auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(40));
      ftxui::Render(screen, panel->Render());
      auto content = screen.ToString();
      require(content.find("VOICEVOX CORE") != std::string::npos,
        "voice shortcut opens offline configuration");
      require(content.find("Confirm") == std::string::npos, "no redundant download confirmation");
    }
    const char *old_dictionary = std::getenv("PAWINGO_JMDICT");
    const bool had_dictionary = old_dictionary != nullptr;
    const std::string previous_dictionary = old_dictionary ? old_dictionary : "";
    pawingo::tests::environment("PAWINGO_JMDICT", argv[2]);
    auto backend = plugin->search("猫", "en");
    require(backend.size() == 2 && backend[0].expression == "猫", "backend vocabulary search");
    require(
      plugin->match(backend[0], "feline").result == pawingo::Match::correct &&
        plugin->match(backend[0], " CAT ").result == pawingo::Match::acceptable,
      "Japanese review assessment accepts a saved meaning alternative without requiring every gloss"
    );
    {
      auto panel = plugin->create_panel("語彙検索", host);
      require(bool(panel), "search panel");
      const auto previous_redraws = host.redraws.load();
      panel->OnEvent(ftxui::Event::Character("猫"));
      panel->OnEvent(ftxui::Event::Return);
      auto rendered = [&] {
        auto screen =
          ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(40));
        ftxui::Render(screen, panel->Render());
        return screen.ToString();
      };
      auto wait_for = [&](const std::string &text) {
        for (int i = 0; i < 100; ++i) {
          if (rendered().find(text) != std::string::npos)
            return true;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
      };
      require(wait_for("EDRDG"), "search results display source");
      for (int i = 0; i < 100 && host.redraws.load() == previous_redraws; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      require(host.redraws.load() > previous_redraws, "query completion requests a host redraw");
      auto *help = dynamic_cast<pawingo::PanelHelpProvider *>(plugin);
      require(help, "search footer provider");
      for (int i = 0; i < 100 && help->panel_help("語彙検索").find("scroll") == std::string::npos;
        ++i) {
        panel->OnEvent(ftxui::Event::Return);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      require(help->panel_help("語彙検索").find("scroll") != std::string::npos &&
          rendered().find("ねこ") != std::string::npos &&
          rendered().find("EDRDG") != std::string::npos,
        "enter opens selected word details with reading and source");
      require(wait_for("No offline examples"),
        "optional example resource is not downloaded implicitly");
      const auto example_index = directory / "examples.tsv";
      std::ofstream(example_index) << "101\t201\t猫がいる。\tThere is a cat.\t猫(ねこ)[01]~\n";
      panel->OnEvent(ftxui::Event::CtrlD);
      for (int i = 0; i < 3; ++i)
        panel->OnEvent(ftxui::Event::ArrowDown);
      panel->OnEvent(ftxui::Event::Return);
      panel->OnEvent(ftxui::Event::Character(example_index.string()));
      panel->OnEvent(ftxui::Event::Return);
      require(wait_for("There is a cat."), "imported independent examples appear in details");
      require(rendered().find("Tatoeba #101") != std::string::npos,
        "example details expose sentence provenance");
      panel->OnEvent(ftxui::Event::Character("ignored"));
      panel->OnEvent(ftxui::Event::Tab);
      panel->OnEvent(ftxui::Event::ArrowDown);
      panel->OnEvent(ftxui::Event::CtrlS);
      require(host.saved.size() == 1 && host.saved[0].expression == "猫" &&
          host.saved[0].reading == "ねこ",
        "detail scrolling keeps the selected word and save preserves its reading");
      panel->OnEvent(ftxui::Event::CtrlD);
      require(panel->OnEvent(ftxui::Event::Escape) &&
          help->panel_help("語彙検索").find("scroll") != std::string::npos,
        "dictionary manager returns to details when opened there");
      require(panel->OnEvent(ftxui::Event::Escape) &&
          help->panel_help("語彙検索").find("enter open") != std::string::npos &&
          rendered().find("ignored") == std::string::npos,
        "escape returns to results without changing hidden query fields");
      panel->OnEvent(ftxui::Event::Character("舌"));
      panel->OnEvent(ftxui::Event::Return);
      require(wait_for("sensitivity"),
        "editing the query makes enter search instead of opening stale details");
      panel->OnEvent(ftxui::Event::Backspace);
      panel->OnEvent(ftxui::Event::Return);
      require(wait_for("feline"), "query can be restored after visiting details");
      panel->OnEvent(ftxui::Event::Tab);
      panel->OnEvent(ftxui::Event::Backspace);
      panel->OnEvent(ftxui::Event::Backspace);
      panel->OnEvent(ftxui::Event::Delete);
      panel->OnEvent(ftxui::Event::Delete);
      panel->OnEvent(ftxui::Event::Character("de"));
      panel->OnEvent(ftxui::Event::Return);
      require(wait_for("Katze"), "search target language input");
      panel->OnEvent(ftxui::Event::CtrlD);
      panel->OnEvent(ftxui::Event::ArrowDown);
      panel->OnEvent(ftxui::Event::Return);
      panel->OnEvent(ftxui::Event::End);
      for (std::size_t i = 0; i < example_index.string().size(); ++i)
        panel->OnEvent(ftxui::Event::Backspace);
      const auto german_pairs = directory / "de.tsv";
      std::ofstream(german_pairs) << "101\t猫がいる。\t301\tEine Katze ist da.\n";
      panel->OnEvent(ftxui::Event::Character(german_pairs.string()));
      panel->OnEvent(ftxui::Event::Return);
      require(wait_for("Offline example resource ready"),
        "target-language sentence pairs import through the manager");
      for (int i = 0; i < 100 && help->panel_help("語彙検索").find("scroll") == std::string::npos;
        ++i) {
        panel->OnEvent(ftxui::Event::Return);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      require(wait_for("Eine Katze ist da."),
        "details select the requested example translation language");
      panel->OnEvent(ftxui::Event::CtrlD);
      panel->OnEvent(ftxui::Event::ArrowUp);
      panel->OnEvent(ftxui::Event::ArrowUp);
      panel->OnEvent(ftxui::Event::Return);
      require(rendered().find("downloads.tatoeba.org") != std::string::npos,
        "example download requires explicit confirmation");
      require(panel->OnEvent(ftxui::Event::Escape), "cancel example download confirmation");
      require(panel->OnEvent(ftxui::Event::Escape),
        "manager returns to the originating detail page");
      require(panel->OnEvent(ftxui::Event::Escape),
        "details return to search after resource management");
      require(!panel->OnEvent(ftxui::Event::Escape), "search delegates back navigation to host");
    }
    if (had_dictionary)
      pawingo::tests::environment("PAWINGO_JMDICT", previous_dictionary);
    else
      pawingo::tests::unset_environment("PAWINGO_JMDICT");
    pawingo::tests::environment("PATH", original_path);
    if (had_data)
      pawingo::tests::environment("XDG_DATA_HOME", original_data);
    else
      pawingo::tests::unset_environment("XDG_DATA_HOME");
    std::filesystem::remove_all(directory);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
