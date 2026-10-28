#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <time.h>

import std;
import pawingo.core;
import pawingo.plugin;
import pawingo.ui;

namespace config = pawingo::config;

namespace {
using pawingo::config::ui_text;

class UiHost : public pawingo::Host, public pawingo::UiRefresh {
public:
  UiHost(pawingo::Store &store, ftxui::ScreenInteractive &screen)
    : store_(store),
      screen_(screen) {
  }

  void save(const pawingo::Vocabulary &word) override {
    store_.save(word);
  }

  std::vector<pawingo::Resource> resources(pawingo::ResourceKind kind,
    const std::string &language) override {
    return store_.resources(kind, language);
  }

  void request_redraw() override {
    screen_.PostEvent(ftxui::Event::Custom);
  }

private:
  pawingo::Store &store_;
  ftxui::ScreenInteractive &screen_;
};

std::filesystem::path data_dir() {
  return pawingo::config::data_path("");
}

std::string date_key(std::time_t value) {
  std::tm local{};
  localtime_r(&value, &local);
  char date[11];
  std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
  return date;
}

ftxui::Element language_flag(const std::string &language) {
  using namespace ftxui;
  for (const auto &row : pawingo::config::application().rows("languages"))
    if (row.size() == 3 && row[0] == language)
      return text(row[2]);
  return text("  ");
}

ftxui::Element activity(pawingo::Store &store) {
  using namespace ftxui;
  using namespace pawingo::ui;
  const auto &defaults = config::application();
  const int weeks = defaults.integer("activity.weeks", 1, 52);
  const int light = defaults.integer("activity.light_threshold", 1, 10000);
  const int heavy = defaults.integer("activity.heavy_threshold", light, 10000);
  const auto marks = defaults.strings("activity.marks");
  if (marks.size() != 4)
    throw std::runtime_error("Activity calendar requires four marks.");
  const auto now = std::time(nullptr);
  std::tm today{};
  localtime_r(&now, &today);
  today.tm_hour = 12;
  today.tm_min = 0;
  today.tm_sec = 0;
  std::tm first = today;
  first.tm_mday -= today.tm_wday + (weeks - 1) * 7;
  first.tm_isdst = -1;
  auto start = std::mktime(&first);
  std::tm tomorrow = today;
  tomorrow.tm_mday += 1;
  tomorrow.tm_hour = 0;
  tomorrow.tm_isdst = -1;
  const auto until = std::mktime(&tomorrow);
  std::map<std::string, int> counts;
  for (auto timestamp : store.review_timestamps(start - 86400, until))
    ++counts[date_key(static_cast<std::time_t>(timestamp))];
  std::array<Elements, 7> rows;
  const auto today_key = date_key(now);
  for (int day = 0; day < weeks * 7; ++day) {
    auto current = first;
    current.tm_mday += day;
    current.tm_isdst = -1;
    const auto date = date_key(std::mktime(&current));
    if (date > today_key) {
      rows[day % 7].push_back(text("   "));
      continue;
    }
    const int count = counts[date];
    const bool today_cell = date == today_key;
    const auto shade = today_cell ? accent : count == 0 ? faint : count < heavy ? olive : ink;
    const std::string mark = today_cell ? defaults.text("activity.today")
      : count == 0                      ? marks[0]
      : count < light                   ? marks[1]
      : count < heavy                   ? marks[2]
                                        : marks[3];
    rows[day % 7].push_back(hbox({text(mark) | color(shade), text("  ")}));
  }
  Elements graph;
  for (auto &row : rows)
    graph.push_back(hbox(std::move(row)));
  return vbox(std::move(graph));
}
} // namespace

int main() {
  try {
    using namespace ftxui;
    using namespace pawingo::ui;
    auto directory = data_dir();
    std::filesystem::create_directories(directory);
    pawingo::Store store((directory / config::application().text("storage.database")).string());
    pawingo::Plugins plugins;
    plugins.discover((directory / config::application().text("storage.plugins")).string());
    if (const char *path =
          std::getenv(config::application().text("storage.plugin_override_env").c_str());
      path && *path)
      plugins.discover(path);
    auto languages = plugins.languages();
    auto screen = ScreenInteractive::Fullscreen();
    UiHost ui_host(store, screen);
    enum class Page { home, features, panel, download };
    Page page = Page::home;
    int choice = 0;
    int feature = 0;
    pawingo::LanguagePlugin *current = nullptr;
    std::vector<std::string> features;
    Component plugin_panel;
    auto focus = Button("", [] {
    });
    auto app = Renderer(focus, [&] {
      Elements content;
      std::string footer;
      if (page == Page::home) {
        content.push_back(activity(store) | hcenter);
        content.push_back(text(""));
        content.push_back(text(""));
        if (languages.empty())
          content.push_back(text(ui_text("no_plugins_installed_yet")) | color(muted) | hcenter);
        for (std::size_t i = 0; i < languages.size(); ++i) {
          const bool selected = choice == static_cast<int>(i);
          const auto language = languages[i]->language();
          std::string native = language;
          for (const auto &row : config::application().rows("languages"))
            if (row.size() == 3 && row[0] == language)
              native = row[1];
          content.push_back(hbox({text(selection(selected)) | color(accent),
                              language_flag(language),
                              text("  "),
                              text(native) | color(selected ? ink : secondary),
                              text("  " + language) | color(muted)}) |
            hcenter);
        }
        content.push_back(text(""));
        auto download = text(ui_text("get_plugins")) |
          color(choice == static_cast<int>(languages.size()) ? ink : secondary);
        content.push_back(
          hbox({text(selection(choice == static_cast<int>(languages.size()))) | color(accent),
            download}) |
          hcenter
        );
        footer = ui_text("select_enter_open_q_quit");
        for (const auto &error : plugins.errors())
          content.push_back(paragraph(error) | color(accent));
      } else if (page == Page::features) {
        content.push_back(hbox({language_flag(current->language()),
                            text("  "),
                            text(current->language()) | color(ink)}) |
          hcenter);
        content.push_back(text(""));
        if (features.empty())
          content.push_back(text(ui_text("no_learning_features_available")));
        for (std::size_t i = 0; i < features.size(); ++i) {
          auto item = text(std::string(selection(feature == static_cast<int>(i))) + features[i]);
          if (feature == static_cast<int>(i))
            item = item | color(accent);
          content.push_back(item | hcenter);
        }
        content.push_back(text(""));
        footer = ui_text("select_enter_open_esc_back");
      } else if (page == Page::download) {
        content = {text(ui_text("get_plugins")) | color(ink),
          text(""),
          text(ui_text("no_plugins_available_for_download_yet")) | color(secondary),
          text("")};
        footer = ui_text("esc_back");
      } else {
        content.push_back(plugin_panel->Render());
        if (auto *help = dynamic_cast<pawingo::PanelHelpProvider *>(current))
          footer = help->panel_help(features[feature]) +
            config::application().text("theme.footer_separator");
        footer += ui_text("esc_back");
      }
      auto body = vbox({wordmark(), text(""), text(""), vbox(std::move(content)) | color(ink)}) |
        size(WIDTH, EQUAL, content_width) | center | flex;
      return vbox({body, text(footer) | color(muted) | hcenter, text("")});
    });
    auto events = CatchEvent(app, [&](Event event) {
      if (page == Page::panel) {
        if (event == Event::Escape) {
          // Let the panel dismiss a local prompt before leaving the feature.
          if (plugin_panel->OnEvent(event))
            return true;
          plugin_panel.reset();
          page = Page::features;
          return true;
        }
        return plugin_panel->OnEvent(event);
      }
      if (event == Event::Escape) {
        if (page != Page::home) {
          page = Page::home;
          return true;
        }
        screen.Exit();
        return true;
      }
      if (page == Page::home) {
        if (event == Event::Character('q')) {
          screen.Exit();
          return true;
        }
        if (event == Event::ArrowUp) {
          choice = std::max(0, choice - 1);
          return true;
        }
        if (event == Event::ArrowDown) {
          choice = std::min(static_cast<int>(languages.size()), choice + 1);
          return true;
        }
        if (event == Event::Return) {
          if (choice == static_cast<int>(languages.size()))
            page = Page::download;
          else {
            current = languages[choice];
            features = current->panels();
            feature = 0;
            page = Page::features;
          }
          return true;
        }
      } else if (page == Page::features) {
        if (event == Event::ArrowUp) {
          feature = std::max(0, feature - 1);
          return true;
        }
        if (event == Event::ArrowDown && !features.empty()) {
          feature = std::min(static_cast<int>(features.size()) - 1, feature + 1);
          return true;
        }
        if (event == Event::Return && !features.empty()) {
          plugin_panel = current->create_panel(features[feature], ui_host);
          if (plugin_panel)
            page = Page::panel;
          return true;
        }
      }
      return false;
    });
    screen.Loop(events);
  } catch (const std::exception &e) {
    std::cerr << "Pawingo: " << e.what() << '\n';
    return 1;
  }
}
