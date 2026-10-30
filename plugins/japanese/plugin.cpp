module;
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

module pawingo.japanese.plugin;

import std;
import pawingo.plugin;
import pawingo.japanese.dictionary;
import pawingo.japanese.examples;
import pawingo.io;
import pawingo.native;
import pawingo.tasks;
import pawingo.ui;
import pawingo.review;
import pawingo.review_ui;
import pawingo.japanese.review;
import pawingo.japanese.search;
import pawingo.japanese.settings;
import pawingo.japanese.voicevox;
import pawingo.japanese.voice_panel;

namespace {
using pawingo::japanese::kana_settings;
using pawingo::japanese::settings;
using pawingo::japanese::ui_text;

struct Kana {
  std::string hira, kata, romaji;
};

const auto vowels = kana_settings().strings("row_labels");
const auto consonants = kana_settings().strings("column_labels");
const auto kana = [] {
  auto rows = kana_settings().rows("cells");
  if (vowels.empty() || consonants.empty() || rows.size() != vowels.size() * consonants.size())
    throw std::runtime_error("Kana chart dimensions do not match its content.");
  std::vector<Kana> result;
  for (const auto &row : rows) {
    if (row.size() != 3)
      throw std::runtime_error("Kana cells require hiragana, katakana and romanization.");
    result.push_back({row[0], row[1], row[2]});
  }
  return result;
}();
const auto feature_labels = [] {
  auto labels = settings().strings("plugin.panels");
  if (labels.size() != 4)
    throw std::runtime_error("Japanese plugin requires four feature labels.");
  return labels;
}();

int kana_index(int row, int column) {
  if (row < 0 || row >= static_cast<int>(vowels.size()) || column < 0 ||
    column >= static_cast<int>(consonants.size()))
    return -1;
  int index = row * consonants.size() + column;
  return kana[index].hira.empty() ? -1 : index;
}

int initial_kana() {
  auto initial = kana_settings().text("initial");
  for (std::size_t i = 0; i < kana.size(); ++i)
    if (kana[i].romaji == initial && !initial.empty())
      return static_cast<int>(i);
  throw std::runtime_error("Initial kana is missing from the chart.");
}

struct State {
  int selected = initial_kana();
  std::mutex mutex;
  std::string message;
  std::function<void()> redraw;
  std::shared_ptr<pawingo::japanese::Voicevox> voicevox;
  // Join/cancel before destroying any state accessed by the worker.
  pawingo::tasks::BackgroundJob job;
};

void set_message(State &s, std::string message) {
  std::lock_guard lock(s.mutex);
  s.message = std::move(message);
}

std::filesystem::path model_path() {
  return pawingo::config::data_path(settings().text("tts.legacy.model"));
}

int run(const std::vector<std::string> &args, std::stop_token stop = {}) {
  auto result = pawingo::native::run_process({.arguments = args,
                                               .poll_interval = std::chrono::milliseconds(
                                                 settings().integer("tts.process_poll_ms", 1, 1000)
                                               )},
    stop);
  return result ? *result : -1;
}

/// Runs a resource/speech operation and projects its completion into panel status.
void start(State &s, std::function<void(std::stop_token)> task) {
  s.job.start(std::move(task), [&s](std::exception_ptr failure) {
    if (failure) {
      try {
        std::rethrow_exception(failure);
      } catch (const std::exception &error) {
        set_message(s, error.what());
      } catch (...) {
        set_message(s, ui_text("background_operation_failed"));
      }
    }
    if (s.redraw)
      s.redraw();
  });
}

void speak(State &s, const pawingo::japanese::SpeechRequest &request, std::stop_token stop) {
  const auto &text = request.text;
  const auto &reading = request.reading;

  auto config = pawingo::japanese::Voicevox::load_config();
  if (!config) {
    set_message(s, config.error().message);
    return;
  }
  if (!config->core.empty()) {
    auto result = s.voicevox->speak(*config, request, stop);
    set_message(s, result ? "" : result.error().message);
    return;
  }
  const auto model = model_path();
  const char *dictionary = std::getenv(settings().text("tts.legacy.dictionary_env").c_str());
  if (!model.empty() && std::filesystem::exists(model) && dictionary && *dictionary) {
    pawingo::io::TemporaryArtifact input(std::filesystem::temp_directory_path());
    pawingo::io::TemporaryArtifact output(std::filesystem::temp_directory_path());
    std::ofstream file(input.file());
    file << (reading.empty() ? text : reading);
    file.close();
    int result = file ? run({settings().text("tts.legacy.open_jtalk"),
                              "-x",
                              dictionary,
                              "-m",
                              model.string(),
                              "-ow",
                              output.file().string(),
                              input.file().string()},
                          stop)
                      : -1;
    if (result == 0)
      result = run({settings().text("tts.legacy.player"), "-q", output.file().string()}, stop);
    if (result == 0) {
      set_message(s, "");
      return;
    }
    set_message(s, ui_text("open_jtalk_failed_trying_espeak_ng"));
  }
  if (stop.stop_requested())
    return;
  if (run({settings().text("tts.legacy.espeak"),
            "-v",
            settings().text("plugin.language"),
            reading.empty() ? text : reading},
        stop) != 0)
    set_message(s, ui_text("speech_unavailable_install_espeak_ng_or"));
  else
    set_message(s, "");
}

ftxui::Component panel(pawingo::Host &host,
  std::shared_ptr<pawingo::japanese::Voicevox> voicevox,
  std::shared_ptr<int> page) {
  using namespace ftxui;
  auto state = std::make_shared<State>();
  state->voicevox = std::move(voicevox);
  if (auto *events =
        static_cast<pawingo::UiRefresh *>(host.capability(pawingo::HostCapability::ui_refresh)))
    state->redraw = [events] {
      events->request_redraw();
    };
  auto focus = Button("", [] {
  });
  auto view = Renderer(focus, [state] {
    using namespace pawingo::ui;
    Elements rows;
    rows.push_back(hbox({text(ui_text("kana_title")) | color(ink) | bold,
                     text(ui_text("kana_scripts")) | color(secondary)}) |
      hcenter);
    rows.push_back(text(""));
    const int selected_row = state->selected / static_cast<int>(consonants.size());
    const int selected_column = state->selected % static_cast<int>(consonants.size());
    Elements headings{text(std::string(settings().integer("layout.kana_label_width", 1, 12), ' '))};
    for (int column = 0; column < static_cast<int>(consonants.size()); ++column)
      headings.push_back(text(consonants[column]) |
        color(column == selected_column ? secondary : faint) |
        size(WIDTH, EQUAL, settings().integer("layout.kana_cell_width", 4, 12)));
    rows.push_back(hbox(std::move(headings)) | hcenter);
    rows.push_back(text(""));
    for (int row = 0; row < static_cast<int>(vowels.size()); ++row) {
      Elements cells{text(vowels[row]) | color(row == selected_row ? ink : muted) |
        size(WIDTH, EQUAL, settings().integer("layout.kana_label_width", 1, 12))};
      for (int column = 0; column < static_cast<int>(consonants.size()); ++column) {
        const int index = kana_index(row, column);
        if (index < 0) {
          cells.push_back(text(std::string(settings().integer("layout.kana_cell_width", 4, 12),
            ' ')));
          continue;
        }
        const auto shade = index == state->selected ? accent : secondary;
        cells.push_back(hbox({text(kana[index].hira), text(" "), text(kana[index].kata)}) |
          color(shade) | size(WIDTH, EQUAL, settings().integer("layout.kana_cell_width", 4, 12)));
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
    rows.push_back(paragraph(message) | color(muted) |
      size(HEIGHT, EQUAL, settings().integer("layout.status_height", 1, 10)));
    return vbox(std::move(rows)) | size(WIDTH, EQUAL, content_width);
  });
  auto chart = CatchEvent(view, [state](Event event) {
    if (event == Event::Return) {
      if (!state->job.running()) {
        std::string text = kana[state->selected].hira;
        set_message(*state, ui_text("playing"));
        start(*state, [ptr = state.get(), text](std::stop_token stop) {
          speak(*ptr, {.text = text}, stop);
        });
      }
      return true;
    }
    const int dx = event == Event::ArrowRight ? 1 : event == Event::ArrowLeft ? -1 : 0;
    const int dy = event == Event::ArrowDown ? 1 : event == Event::ArrowUp ? -1 : 0;
    if (!dx && !dy)
      return false;
    int row = state->selected / static_cast<int>(consonants.size());
    int column = state->selected % static_cast<int>(consonants.size());
    while (true) {
      row += dy;
      column += dx;
      if (row < 0 || row >= static_cast<int>(vowels.size()) || column < 0 ||
        column >= static_cast<int>(consonants.size()))
        break;
      int index = kana_index(row, column);
      if (index >= 0) {
        state->selected = index;
        break;
      }
    }
    return true;
  });
  auto management = pawingo::japanese::voicevox_panel(state->voicevox, host);
  auto pages = Container::Tab({chart, management}, page.get());
  return CatchEvent(pages, [page, management](Event event) {
    if (*page == 0 && event == Event::Character('d')) {
      *page = 1;
      return true;
    }
    if (*page == 1 && event == Event::Escape) {
      if (!management->OnEvent(event))
        *page = 0;
      return true;
    }
    return false;
  });
}

class Japanese : public pawingo::LanguagePlugin, public pawingo::PanelHelpProvider {
public:
  pawingo::PanelHelpProvider *panel_help_provider() override {
    return this;
  }

  std::string panel_help(const std::string &id) const override {
    if (id == feature_labels[1])
      return review_help_ ? review_help_() : pawingo::config::ui_text("esc_back");
    std::string shortcuts;
    if (id == feature_labels[3] || (id == feature_labels[2] && kana_page_ && *kana_page_ == 1))
      shortcuts = ui_text("tab_field_ctrl_p_preview");
    else if (id == feature_labels[0] && search_help_)
      shortcuts = search_help_();
    else
      shortcuts = ui_text("select_enter_speak_d_voice");
    return shortcuts + pawingo::config::application().text("theme.footer_separator") +
      pawingo::config::ui_text("esc_back");
  }

  std::string language() const override {
    return settings().text("plugin.name");
  }

  std::vector<pawingo::Vocabulary> search(const std::string &query,
    const std::string &target) override {
    return dictionary_->search(query, target, pawingo::japanese::dictionary_paths());
  }

  pawingo::MatchResult match(const pawingo::Vocabulary &v, const std::string &answer) override {
    return pawingo::review::match_meaning(v, answer);
  }

  std::vector<std::string> panels() const override {
    return feature_labels;
  }

  ftxui::Component create_panel(const std::string &id, pawingo::Host &host) override {
    if (id == feature_labels[1]) {
      auto *reviews = static_cast<pawingo::ui::ReviewProvider *>(
        host.capability(pawingo::HostCapability::review)
      );
      if (!reviews) {
        review_help_ = {};
        return ftxui::Renderer([] {
          return ftxui::paragraph(ui_text("review_unavailable")) |
            ftxui::color(pawingo::ui::muted) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, pawingo::ui::content_width);
        });
      }
      auto review = reviews->create_review_panel(
        settings().text("plugin.language"),
        [this](const pawingo::Vocabulary &word, const std::string &answer) {
          return match(word, answer);
        },
        pawingo::japanese::review_content(voicevox_)
      );
      review_help_ = std::move(review.help);
      return review.component;
    }
    if (id == feature_labels[3])
      return pawingo::japanese::voicevox_panel(voicevox_, host);
    if (id == feature_labels[2]) {
      kana_page_ = std::make_shared<int>(0);
      return panel(host, voicevox_, kana_page_);
    }
    if (id == feature_labels[0]) {
      auto search = pawingo::japanese::search_panel(
        dictionary_,
        host,
        [service = voicevox_](const pawingo::japanese::SpeechRequest &request,
          std::stop_token stop) {
          State state;
          state.voicevox = service;
          speak(state, request, stop);
          return state.message;
        },
        examples_
      );
      search_help_ = std::move(search.help);
      return search.component;
    }
    return nullptr;
  }

private:
  std::shared_ptr<int> kana_page_;
  std::shared_ptr<pawingo::japanese::Voicevox> voicevox_ =
    std::make_shared<pawingo::japanese::Voicevox>();
  std::shared_ptr<pawingo::japanese::Dictionary> dictionary_ =
    std::make_shared<pawingo::japanese::Dictionary>();
  std::shared_ptr<pawingo::japanese::Examples> examples_ =
    std::make_shared<pawingo::japanese::Examples>();
  std::function<std::string()> search_help_, review_help_;
};

pawingo::LanguagePlugin *create() {
  return new Japanese;
}

void destroy(pawingo::LanguagePlugin *p) {
  delete p;
}

const auto plugin_id = settings().text("plugin.id");
const auto plugin_name = settings().text("plugin.name");
const pawingo::PluginDescriptor descriptor{pawingo::plugin_api_version,
  pawingo::plugin_abi_tag,
  plugin_id.c_str(),
  plugin_name.c_str(),
  create,
  destroy};
} // namespace

const pawingo::PluginDescriptor *pawingo::japanese::descriptor() {
  return &::descriptor;
}
