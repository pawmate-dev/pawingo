#include "pawingo/plugin.hpp"
#include "pawingo/ui/theme.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <mutex>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace {
struct Kana {
  const char *hira;
  const char *kata;
  const char *romaji;
};
constexpr std::array<Kana, 51> kana{{{"あ", "ア", "a"},
  {"い", "イ", "i"},
  {"う", "ウ", "u"},
  {"え", "エ", "e"},
  {"お", "オ", "o"},
  {"か", "カ", "ka"},
  {"き", "キ", "ki"},
  {"く", "ク", "ku"},
  {"け", "ケ", "ke"},
  {"こ", "コ", "ko"},
  {"さ", "サ", "sa"},
  {"し", "シ", "shi"},
  {"す", "ス", "su"},
  {"せ", "セ", "se"},
  {"そ", "ソ", "so"},
  {"た", "タ", "ta"},
  {"ち", "チ", "chi"},
  {"つ", "ツ", "tsu"},
  {"て", "テ", "te"},
  {"と", "ト", "to"},
  {"な", "ナ", "na"},
  {"に", "ニ", "ni"},
  {"ぬ", "ヌ", "nu"},
  {"ね", "ネ", "ne"},
  {"の", "ノ", "no"},
  {"は", "ハ", "ha"},
  {"ひ", "ヒ", "hi"},
  {"ふ", "フ", "fu"},
  {"へ", "ヘ", "he"},
  {"ほ", "ホ", "ho"},
  {"ま", "マ", "ma"},
  {"み", "ミ", "mi"},
  {"む", "ム", "mu"},
  {"め", "メ", "me"},
  {"も", "モ", "mo"},
  {"や", "ヤ", "ya"},
  {"", "", ""},
  {"ゆ", "ユ", "yu"},
  {"", "", ""},
  {"よ", "ヨ", "yo"},
  {"ら", "ラ", "ra"},
  {"り", "リ", "ri"},
  {"る", "ル", "ru"},
  {"れ", "レ", "re"},
  {"ろ", "ロ", "ro"},
  {"わ", "ワ", "wa"},
  {"", "", ""},
  {"", "", ""},
  {"", "", ""},
  {"を", "ヲ", "wo"},
  {"ん", "ン", "n"}}};
// Vowel rows and consonant columns follow a right-to-left gojuon chart.
int kana_index(int row, int column) {
  if (row < 0 || row >= 5 || column < 0 || column >= 10)
    return -1;
  const int group = 9 - column;
  const int index = group == 9 && row == 1 ? 50 : group * 5 + row;
  return *kana[index].hira ? index : -1;
}
struct State {
  int selected = 0;
  bool confirm_download = false;
  bool installed = false;
  std::mutex mutex;
  std::string message;
  std::atomic<bool> busy{false};
  // Join/cancel before destroying any state accessed by the worker.
  std::jthread worker;
};
void set_message(State &s, std::string message) {
  std::lock_guard lock(s.mutex);
  s.message = std::move(message);
}
std::filesystem::path model_path() {
  const char *data = std::getenv("XDG_DATA_HOME");
  if (data && *data)
    return std::filesystem::path(data) / "pawingo/tts/japanese/mei_normal.htsvoice";
  const char *home = std::getenv("HOME");
  return home && *home
    ? std::filesystem::path(home) / ".local/share/pawingo/tts/japanese/mei_normal.htsvoice"
    : std::filesystem::path{};
}
bool model_installed() {
  const auto path = model_path();
  if (path.empty())
    return false;
  std::error_code error;
  if (std::filesystem::file_size(path, error) != 862503 || error)
    return false;
  std::ifstream input(path);
  std::string line;
  std::getline(input, line);
  return line == "[GLOBAL]";
}
int run(const std::vector<std::string> &args, std::stop_token stop = {}) {
  if (stop.stop_requested())
    return -1;
  std::vector<char *> argv;
  for (const auto &arg : args)
    argv.push_back(const_cast<char *>(arg.c_str()));
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attributes, 0);
  pid_t pid = -1;
  int result = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  if (result != 0)
    return -1;
  int status = 0;
  for (;;) {
    auto waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid)
      return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (waited < 0 && errno != EINTR)
      return -1;
    if (stop.stop_requested()) {
      kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
}
void start(State &s, std::function<void(std::stop_token)> task) {
  if (s.busy.exchange(true))
    return;
  auto *screen = ftxui::ScreenInteractive::Active();
  s.worker = std::jthread([&s, screen, task = std::move(task)](std::stop_token stop) {
    try {
      task(stop);
    } catch (const std::exception &e) {
      set_message(s, e.what());
    }
    s.busy = false;
    if (screen && !stop.stop_requested())
      screen->PostEvent(ftxui::Event::Custom);
  });
}
void download(State &s, std::stop_token stop) {
  const auto target = model_path();
  if (target.empty()) {
    set_message(s, "Set HOME or XDG_DATA_HOME to download the voice.");
    return;
  }
  try {
    std::filesystem::create_directories(target.parent_path());
    auto temporary = target;
    temporary += ".part";
    if (run({"curl",
              "--fail",
              "--location",
              "--silent",
              "--show-error",
              "--max-time",
              "60",
              "--output",
              temporary.string(),
              "https://raw.githubusercontent.com/r9y9/pyopenjtalk/master/pyopenjtalk/htsvoice/"
              "mei_normal.htsvoice"},
          stop) != 0) {
      std::filesystem::remove(temporary);
      set_message(s, "Download failed; check network and curl.");
      return;
    }
    // Verify the fixed voice file before making it available for synthesis.
    auto check = temporary;
    check += ".sha256";
    // sha256sum checks relative names from the current directory; provide the absolute path
    // instead.
    {
      std::ofstream out(check);
      out << "f3be49a6838904a6c218790b64e07c3e83c1886e995dca284b413caab19184de  "
          << temporary.string() << "\n";
    }
    int verified = run({"sha256sum", "--check", "--status", check.string()}, stop);
    std::filesystem::remove(check);
    if (verified != 0) {
      std::filesystem::remove(temporary);
      set_message(s, "Voice checksum failed.");
      return;
    }
    std::filesystem::rename(temporary, target);
    set_message(s, "Mei installed. Open JTalk and its dictionary are required.");
  } catch (const std::exception &e) {
    set_message(s, std::string("Download failed: ") + e.what());
  }
}
void speak(State &s, const std::string &text, std::stop_token stop) {
  const auto model = model_path();
  const char *dictionary = std::getenv("PAWINGO_OPENJTALK_DICT");
  if (!model.empty() && std::filesystem::exists(model) && dictionary && *dictionary) {
    char input[] = "/tmp/pawingo-kana-input-XXXXXX";
    char output[] = "/tmp/pawingo-kana-audio-XXXXXX";
    int in = mkstemp(input), out = mkstemp(output);
    if (in >= 0 && out >= 0) {
      auto written = write(in, text.data(), text.size());
      close(in);
      close(out);
      int result = written == static_cast<ssize_t>(text.size())
        ? run({"open_jtalk", "-x", dictionary, "-m", model.string(), "-ow", output, input}, stop)
        : -1;
      if (result == 0)
        result = run({"aplay", "-q", output}, stop);
      unlink(input);
      unlink(output);
      if (result == 0) {
        set_message(s, "");
        return;
      }
    } else {
      if (in >= 0) {
        close(in);
        unlink(input);
      }
      if (out >= 0) {
        close(out);
        unlink(output);
      }
    }
    set_message(s, "Open JTalk failed; trying eSpeak NG.");
  }
  if (stop.stop_requested())
    return;
  if (run({"espeak-ng", "-v", "ja", text}, stop) != 0)
    set_message(s, "Speech unavailable. Install eSpeak NG or configure Open JTalk.");
  else
    set_message(s, "");
}

ftxui::Component panel() {
  using namespace ftxui;
  auto state = std::make_shared<State>();
  state->installed = model_installed();
  if (state->installed)
    set_message(*state, "Mei voice installed.");
  auto focus = Button("", [] {
  });
  auto view = Renderer(focus, [state] {
    using namespace pawingo::ui;
    Elements rows;
    rows.push_back(hbox({text("五十音図") | color(ink) | bold,
                     text("      ひらがな  ·  カタカナ") | color(secondary)}) |
      hcenter);
    rows.push_back(text(""));
    const int selected_row = state->selected == 50 ? 1 : state->selected % 5;
    const int selected_column = state->selected == 50 ? 0 : 9 - state->selected / 5;
    constexpr std::array<const char *, 10>
      consonants{"w", "r", "y", "m", "h", "n", "t", "s", "k", "·"};
    constexpr std::array<const char *, 5> vowels{"a", "i", "u", "e", "o"};
    Elements headings{text("    ")};
    for (int column = 0; column < 10; ++column)
      headings.push_back(text(consonants[column]) |
        color(column == selected_column ? secondary : faint) | size(WIDTH, EQUAL, 6));
    rows.push_back(hbox(std::move(headings)) | hcenter);
    rows.push_back(text(""));
    for (int row = 0; row < 5; ++row) {
      Elements cells{
        text(vowels[row]) | color(row == selected_row ? ink : muted) | size(WIDTH, EQUAL, 4)};
      for (int column = 0; column < 10; ++column) {
        const int index = kana_index(row, column);
        if (index < 0) {
          cells.push_back(text("      "));
          continue;
        }
        const auto shade = index == state->selected ? accent : secondary;
        cells.push_back(hbox({text(kana[index].hira), text(" "), text(kana[index].kata)}) |
          color(shade) | size(WIDTH, EQUAL, 6));
      }
      rows.push_back(hbox(std::move(cells)) | hcenter);
      rows.push_back(text(""));
    }
    const auto &selected = kana[state->selected];
    rows.push_back(hbox({text(selected.hira) | color(accent),
                     text("  "),
                     text(selected.kata) | color(ink),
                     text("  "),
                     text(selected.romaji) | color(secondary)}) |
      hcenter);
    std::string message;
    {
      std::lock_guard lock(state->mutex);
      message = state->message;
    }
    rows.push_back(paragraph(message) | color(muted) | size(HEIGHT, EQUAL, 3));
    return vbox(std::move(rows)) | size(WIDTH, EQUAL, content_width);
  });
  return CatchEvent(view, [state](Event event) {
    if (event == Event::Character('d')) {
      if (state->busy)
        return true;
      state->installed = model_installed();
      if (state->installed) {
        state->confirm_download = false;
        set_message(*state, "Mei is already installed. No download needed.");
      } else if (!state->confirm_download) {
        state->confirm_download = true;
        set_message(*state,
          "Download Mei (0.9 MB) from GitHub? d Confirm / Esc Cancel. CC BY 3.0, Nagoya Institute "
          "of Technology.");
      } else {
        state->confirm_download = false;
        set_message(*state, "Downloading Mei…");
        start(*state, [ptr = state.get()](std::stop_token stop) {
          download(*ptr, stop);
        });
      }
      return true;
    }
    if (event == Event::Escape && state->confirm_download) {
      state->confirm_download = false;
      set_message(*state, "");
      return true;
    }
    if (event == Event::Return) {
      if (!state->busy) {
        std::string text = kana[state->selected].hira;
        set_message(*state, "Playing…");
        start(*state, [ptr = state.get(), text](std::stop_token stop) {
          speak(*ptr, text, stop);
        });
      }
      return true;
    }
    const int dx = event == Event::ArrowRight ? 1 : event == Event::ArrowLeft ? -1 : 0;
    const int dy = event == Event::ArrowDown ? 1 : event == Event::ArrowUp ? -1 : 0;
    if (!dx && !dy)
      return false;
    int row = state->selected == 50 ? 1 : state->selected % 5;
    int column = state->selected == 50 ? 0 : 9 - state->selected / 5;
    while (true) {
      row += dy;
      column += dx;
      if (row < 0 || row >= 5 || column < 0 || column >= 10)
        break;
      int index = kana_index(row, column);
      if (index >= 0) {
        state->selected = index;
        break;
      }
    }
    return true;
  });
}
class Japanese : public pawingo::LanguagePlugin, public pawingo::PanelHelpProvider {
public:
  std::string panel_help(const std::string &) const override {
    return "← ↑ ↓ →  select    enter  speak    d  voice";
  }
  std::string language() const override {
    return "Japanese";
  }
  std::vector<pawingo::Vocabulary> search(const std::string &, const std::string &) override {
    return {};
  }
  pawingo::MatchResult match(const pawingo::Vocabulary &v, const std::string &answer) override {
    return {answer == v.meaning ? pawingo::Match::correct : pawingo::Match::incorrect, ""};
  }
  std::vector<std::string> panels() const override {
    return {"五十音図"};
  }
  ftxui::Component create_panel(const std::string &id, pawingo::Host &) override {
    return id == "五十音図" ? panel() : nullptr;
  }
};
pawingo::LanguagePlugin *create() {
  return new Japanese;
}
void destroy(pawingo::LanguagePlugin *p) {
  delete p;
}
const pawingo::PluginDescriptor descriptor{pawingo::plugin_api_version,
  pawingo::plugin_abi_tag,
  "japanese",
  "Japanese",
  create,
  destroy};
} // namespace
extern "C" const pawingo::PluginDescriptor *pawingo_plugin_v1() {
  return &descriptor;
}
