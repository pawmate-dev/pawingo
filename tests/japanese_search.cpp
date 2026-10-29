#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <stdlib.h>

import std;
import pawingo.japanese.search;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TestHost : pawingo::Host {
  std::filesystem::path dictionary;
  std::vector<pawingo::Vocabulary> saved;

  void save(const pawingo::Vocabulary &word) override {
    saved.push_back(word);
  }

  std::vector<pawingo::Resource> resources(pawingo::ResourceKind kind,
    const std::string &) override {
    if (kind != pawingo::ResourceKind::dictionary)
      return {};
    return {{"fixture", "fixture", kind, "ja", dictionary.string(), true, 0}};
  }
};

int main(int argc, char **argv) {
  using namespace pawingo::japanese;
  using namespace ftxui;
  try {
    require(argc == 2, "dictionary fixture required");
    auto root = std::filesystem::temp_directory_path() /
      ("pawingo-search-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", root.string().c_str());
    _putenv_s("PAWINGO_JMDICT", "");
#else
    setenv("XDG_DATA_HOME", root.c_str(), 1);
    unsetenv("PAWINGO_JMDICT");
#endif

    struct Cleanup {
      std::filesystem::path path;

      ~Cleanup() {
        std::filesystem::remove_all(path);
      }
    } cleanup{root};

    const auto index = root / "index.tsv";
    std::ofstream(index) << "1\t101\t猫がいる。\tThere is a cat.\t猫(ねこ)[01]~\n"
                            "2\t102\t猫が寝ている。\tA cat is sleeping.\t猫(ねこ)[01]~\n";
    auto examples = std::make_shared<Examples>(root / "examples.sqlite3");
    require(bool(examples->install_index(index)), "install example fixture");
    TestHost host;
    host.dictionary = argv[1];
    std::mutex mutex;
    std::vector<SpeechRequest> requests;
    std::atomic<bool> release = true, cancelled = false;
    auto panel = search_panel(
      std::make_shared<Dictionary>(),
      host,
      [&](const SpeechRequest &request, std::stop_token stop) {
        {
          std::lock_guard lock(mutex);
          requests.push_back(request);
        }
        while (!release && !stop.stop_requested())
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancelled = stop.stop_requested();
        return "speech finished";
      },
      examples
    );
    auto render = [&] {
      auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(24));
      Render(screen, panel.component->Render());
      return screen.ToString();
    };
    int checkpoint = 0;
    auto await = [&](auto predicate) {
      ++checkpoint;
      for (int i = 0; i < 2000; ++i) {
        if (predicate())
          return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      throw std::runtime_error("background operation timed out at checkpoint " +
        std::to_string(checkpoint));
    };
    auto size = [&] {
      std::lock_guard lock(mutex);
      return requests.size();
    };
    auto speak = [&](std::size_t expected) {
      // Worker completion and its UI publication may occur on consecutive ticks.
      await([&] {
        if (size() >= expected)
          return true;
        panel.component->OnEvent(Event::CtrlP);
        return false;
      });
      if (release)
        await([&] {
          return render().find("speech finished") != std::string::npos;
        });
    };
    panel.component->OnEvent(Event::Character("猫"));
    panel.component->OnEvent(Event::Return);
    await([&] {
      return render().find("feline") != std::string::npos;
    });
    await([&] {
      panel.component->OnEvent(Event::Return);
      return panel.help().find("shift+tab") != std::string::npos;
    });
    await([&] {
      return render().find("There is a cat.") != std::string::npos;
    });
    speak(1);
    {
      std::lock_guard lock(mutex);
      require(requests[0].text == "猫" && requests[0].reading == "ねこ",
        "word pronunciation preserves dictionary reading");
    }
    panel.component->OnEvent(Event::Tab);
    release = false;
    speak(2);
    panel.component->OnEvent(Event::Tab);
    panel.component->OnEvent(Event::CtrlP);
    require(size() == 2, "selection remains responsive without overlapping playback");
    {
      std::lock_guard lock(mutex);
      require(requests[1].text == "猫がいる。" && requests[1].reading.empty(),
        "sentence uses full Japanese text without the word reading or translation");
    }
    panel.component->OnEvent(Event::CtrlS);
    require(host.saved.size() == 1 && host.saved[0].expression == "猫",
      "saving an example selection still saves the dictionary word");
    release = true;
    speak(3);
    {
      std::lock_guard lock(mutex);
      require(requests[2].text == "猫が寝ている。", "next example becomes the next speech target");
    }
    panel.component->OnEvent(Event::Tab);
    speak(4);
    {
      std::lock_guard lock(mutex);
      require(requests[3].text == "猫", "selection wraps from final example to word");
    }
    panel.component->OnEvent(Event::TabReverse);
    release = false;
    speak(5);
    require(panel.component->OnEvent(Event::Escape), "Escape returns from details");
    await([&] {
      return cancelled.load();
    });
    require(!panel.component->OnEvent(Event::Escape), "search delegates Escape to host");
    panel.component->OnEvent(Event::Return);
    await([&] {
      return panel.help().find("shift+tab") != std::string::npos;
    });
    release = true;
    speak(6);
    {
      std::lock_guard lock(mutex);
      require(requests[4].text == "猫が寝ている。" && requests[5].text == "猫",
        "reverse navigation selects last example; reopening resets to word");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
