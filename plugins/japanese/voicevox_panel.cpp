#include "voicevox_panel.hpp"
#include "pawingo/tasks/background_job.hpp"
#include "pawingo/ui/theme.hpp"
#include "settings.hpp"
#include "voicevox_install.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <mutex>
#include <optional>
#include <sstream>

namespace pawingo::japanese {
ftxui::Component voicevox_panel(std::shared_ptr<Voicevox> service, pawingo::Host &host) {
  using namespace ftxui;

  struct State {
    VoicevoxConfig config;
    std::vector<VoicevoxStyle> styles;
    int selected = 0;
    bool confirm_download = false;
    int term_line = 0;
    std::optional<VoicevoxInstallation> installation;
    std::mutex mutex;
    std::string message;
    pawingo::tasks::BackgroundJob job;
  };

  auto s = std::make_shared<State>();
  auto loaded = Voicevox::load_config();
  if (loaded)
    s->config = *loaded;
  else
    s->message = loaded.error().message;
  InputOption input_options;
  input_options.multiline = false;
  input_options.transform = [](InputState input) {
    using namespace pawingo::ui;
    return input.element | color(input.focused ? ink : input.is_placeholder ? muted : secondary);
  };
  auto button_options = ButtonOption::Simple();
  button_options.transform = [](const EntryState &entry) {
    using namespace pawingo::ui;
    return text(std::string(selection(entry.focused, true)) + entry.label) |
      color(entry.focused ? accent : secondary);
  };
  auto core = Input(&s->config.core, ui_text("core_library_file"), input_options);
  auto runtime = Input(&s->config.runtime, ui_text("voicevox_onnx_runtime_file"), input_options);
  auto dictionary =
    Input(&s->config.dictionary, ui_text("open_jtalk_dictionary_directory"), input_options);
  auto model = Input(&s->config.model, ui_text("voice_model_vvm_file"), input_options);
  auto notify = [&host] {
    if (auto *ui = dynamic_cast<pawingo::UiRefresh *>(&host))
      ui->request_redraw();
  };
  auto launch = [s, notify](auto work) {
    s->job.start(work, [s = s.get(), notify](std::exception_ptr error) {
      if (error) {
        std::lock_guard lock(s->mutex);
        s->message = ui_text("voice_operation_failed");
      }
      notify();
    });
  };
  auto download = Button(
    ui_text("download_install"),
    [s] {
      if (s->job.running())
        return;
      auto installed = installed_voicevox();
      if (installed) {
        s->config = *installed;
        auto result = Voicevox::save_config(s->config);
        std::lock_guard lock(s->mutex);
        s->message =
          result ? ui_text("reusing_installed_resources_load_styles_to") : result.error().message;
      } else {
        s->confirm_download = true;
        std::lock_guard lock(s->mutex);
        s->message = ui_text("download_cpu_runtime_dictionary_and_starter");
      }
    },
    button_options
  );
  auto save = Button(
    ui_text("save"),
    [s] {
      auto result = Voicevox::save_config(s->config);
      std::lock_guard lock(s->mutex);
      s->message = result ? ui_text("saved_learning_panels_now_use_this") : result.error().message;
    },
    button_options
  );
  auto scan = Button(
    ui_text("load_styles"),
    [s, service, launch] {
      if (s->job.running())
        return;
      auto config = s->config;
      launch([s = s.get(), service, config](std::stop_token stop) {
        auto result = service->styles(config);
        if (stop.stop_requested())
          return;
        std::lock_guard lock(s->mutex);
        if (result) {
          s->styles = *result;
          s->selected = 0;
          for (std::size_t i = 0; i < s->styles.size(); ++i)
            if (s->styles[i].id == config.style)
              s->selected = static_cast<int>(i);
          s->message = ui_text("choose_enter_select_ctrl_p_preview");
        } else
          s->message = result.error().message;
      });
    },
    button_options
  );
  auto styles = Button(
    ui_text("styles"),
    [] {
    },
    button_options
  );
  auto fields =
    Container::Vertical({core, runtime, dictionary, model, download, save, scan, styles});
  auto view = Renderer(fields, [s, core, runtime, dictionary, model, download, save, scan] {
    using namespace pawingo::ui;
    std::lock_guard lock(s->mutex);
    if (s->installation) {
      std::istringstream input(s->installation->terms);
      std::string line;
      Elements terms;
      int index = 0;
      while (std::getline(input, line)) {
        auto element = paragraph(line.empty() ? " " : line) | color(secondary);
        if (index++ == s->term_line)
          element = element | focus;
        terms.push_back(element);
      }
      return vbox({text(ui_text("voicevox_installation_terms")) | color(ink),
               text(""),
               vbox(std::move(terms)) | vscroll_indicator | yframe |
                 size(HEIGHT, EQUAL, settings().integer("layout.terms_height", 1, 30)),
               text(""),
               text(ui_text("scroll_enter_accept_install_esc_discard")) | color(accent)}) |
        size(WIDTH, EQUAL, content_width);
    }
    auto row = [](const std::string &label, Component input) {
      bool focused = input->Focused();
      return hbox({text(selection(focused, true)) | color(accent),
        text(label) | color(focused ? accent : muted) |
          size(WIDTH, EQUAL, settings().integer("layout.field_label_width", 1, 40)),
        input->Render() | flex});
    };
    Elements lines{text(ui_text("voicevox_core")) | color(ink),
      text(""),
      row(ui_text("core"), core),
      row(ui_text("runtime"), runtime),
      row(ui_text("dictionary"), dictionary),
      row(ui_text("model"), model),
      text(""),
      download->Render(),
      hbox({save->Render(), text("  "), scan->Render()}),
      text("")};
    int start = std::max(0, s->selected - settings().integer("layout.voice_list_rows", 1, 10) / 2);
    for (int i = start; i < std::min(start + settings().integer("layout.voice_list_rows", 1, 10),
                              static_cast<int>(s->styles.size()));
      ++i)
      lines.push_back(text(std::string(selection(i == s->selected, true)) + s->styles[i].name +
                        "  #" + std::to_string(s->styles[i].id)) |
        color(i == s->selected ? accent : secondary));
    lines.push_back(text(ui_text("style") + std::to_string(s->config.style)) | color(muted));
    lines.push_back(paragraph(s->message) | color(muted) |
      size(HEIGHT, EQUAL, settings().integer("layout.status_height", 1, 10)));
    return vbox(std::move(lines)) | size(WIDTH, EQUAL, content_width);
  });
  return CatchEvent(view, [s, service, launch, styles, notify](Event e) {
    if (s->confirm_download) {
      if (e == Event::Escape) {
        s->confirm_download = false;
        std::lock_guard lock(s->mutex);
        s->message.clear();
        return true;
      }
      if (e == Event::Return) {
        s->confirm_download = false;
        {
          std::lock_guard lock(s->mutex);
          s->message = ui_text("preparing_download");
        }
        launch([s = s.get(), notify](std::stop_token stop) {
          auto result = prepare_voicevox(stop, [s, notify](std::string message) {
            std::lock_guard lock(s->mutex);
            s->message = std::move(message);
            notify();
          });
          std::lock_guard lock(s->mutex);
          if (result) {
            s->installation = std::move(*result);
            s->term_line = 0;
          } else
            s->message = result.error().message;
        });
      }
      return true;
    }
    {
      std::lock_guard lock(s->mutex);
      if (s->installation) {
        if (e == Event::Escape) {
          s->installation.reset();
          s->message = ui_text("installation_discarded");
          return true;
        }
        if (e == Event::ArrowDown) {
          int lines =
            std::count(s->installation->terms.begin(), s->installation->terms.end(), '\n');
          s->term_line = std::min(s->term_line + 1, lines);
          return true;
        }
        if (e == Event::ArrowUp) {
          s->term_line = std::max(0, s->term_line - 1);
          return true;
        }
        if (e == Event::Return) {
          auto result = commit_voicevox(*s->installation);
          if (result) {
            s->config = *result;
            auto saved = Voicevox::save_config(s->config);
            s->message =
              saved ? ui_text("installed_load_styles_to_choose_a") : saved.error().message;
            s->installation.reset();
          } else {
            s->message = result.error().message;
            s->installation.reset();
          }
          return true;
        }
        return true;
      }
    }

    if (e == Event::Escape && s->job.running()) {
      s->job.request_stop();
      std::lock_guard lock(s->mutex);
      s->message = ui_text("cancelling");
      return true;
    }
    if (e == Event::CtrlP) {
      if (!s->job.running()) {
        auto config = s->config;
        launch([s = s.get(), service, config](std::stop_token stop) {
          auto result = service->speak(config, settings().text("tts.preview"), stop);
          std::lock_guard lock(s->mutex);
          s->message = result ? "" : result.error().message;
        });
      }
      return true;
    }
    if (!styles->Focused())
      return false;
    std::lock_guard lock(s->mutex);
    if (s->styles.empty())
      return false;
    if (e == Event::ArrowDown) {
      s->selected = std::min(s->selected + 1, static_cast<int>(s->styles.size()) - 1);
      return true;
    }
    if (e == Event::ArrowUp) {
      s->selected = std::max(0, s->selected - 1);
      return true;
    }
    if (e == Event::Return) {
      s->config.style = s->styles[s->selected].id;
      s->message = ui_text("selected_save_to_apply");
      return true;
    }
    return false;
  });
}
} // namespace pawingo::japanese
