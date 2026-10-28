module;
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

module pawingo.japanese.search;

import std;
import pawingo.net;
import pawingo.tasks;
import pawingo.ui;
import pawingo.japanese.settings;

namespace pawingo::japanese {
namespace {
// Resource discovery belongs to the host integration, not the dictionary parser.
std::vector<std::filesystem::path> panel_dictionary_paths(Host &host) {
  std::vector<std::filesystem::path> paths;
  for (const auto &resource :
    host.resources(ResourceKind::dictionary, settings().text("plugin.language"))) {
    std::error_code error;
    if (resource.enabled && std::filesystem::is_regular_file(resource.location, error))
      paths.emplace_back(resource.location);
  }
  for (const auto &path : dictionary_paths())
    if (std::find(paths.begin(), paths.end(), path) == paths.end())
      paths.push_back(path);
  return paths;
}
enum class Page { search, dictionary, confirm, import };

struct SearchState {
  std::string query, target = settings().text("dictionary.target"), file;
  Page page = Page::search;
  int field = 0, selected = 0, resource_choice = 0;
  std::mutex mutex;
  std::vector<Vocabulary> results;
  std::string message;
  std::function<void()> redraw;
  pawingo::tasks::BackgroundJob job;
};

void message(SearchState &state, std::string text) {
  std::lock_guard lock(state.mutex);
  state.message = std::move(text);
}

void publish(SearchState &state, std::vector<Vocabulary> results, std::string text) {
  std::lock_guard lock(state.mutex);
  state.results = std::move(results);
  state.message = std::move(text);
}

/// Schedules work while keeping failure presentation and host redraw at the UI boundary.
void launch(SearchState &state, std::function<void(std::stop_token)> work) {
  state.job.start(std::move(work), [&state](std::exception_ptr failure) {
    if (failure) {
      try {
        std::rethrow_exception(failure);
      } catch (const std::exception &error) {
        message(state, error.what());
      } catch (...) {
        message(state, ui_text("background_operation_failed"));
      }
    }
    if (state.redraw)
      state.redraw();
  });
}

// Download into a unique file and validate it before replacing an installed dictionary.
void fetch(Dictionary &dictionary, SearchState &state, std::stop_token stop) {
  pawingo::net::TemporaryDownload temporary(dictionary_path().parent_path());
  pawingo::net::DownloadRequest request;
  request.url = settings().text("dictionary.url");
  request.destination = temporary.file();
  request.max_bytes = settings().integer("dictionary.download_bytes", 1, 1073741824);
  request.timeout = std::chrono::seconds(settings().integer("dictionary.timeout_seconds", 1, 3600));
  auto result = pawingo::net::download_https(request, stop);
  if (!result)
    throw std::runtime_error(result.error().message);
  message(state, ui_text("reading_jmdict"));
  dictionary.install(temporary.file(), stop);
}
} // namespace

SearchPanel search_panel(std::shared_ptr<Dictionary> dictionary, Host &host, Speech speech) {
  using namespace ftxui;
  using namespace pawingo::ui;
  auto state = std::make_shared<SearchState>();
  if (auto *events = dynamic_cast<pawingo::UiRefresh *>(&host))
    state->redraw = [events] {
      events->request_redraw();
    };
  InputOption options;
  options.multiline = false;
  options.transform = [](InputState input) {
    return input.element | color(input.is_placeholder ? muted : input.focused ? ink : secondary);
  };
  auto query = Input(&state->query, ui_text("word_reading_or_meaning"), options);
  auto target = Input(&state->target, settings().text("dictionary.target"), options);
  auto file = Input(&state->file, ui_text("path_to_jmdict_xml_gz"), options);
  auto fields = Container::Vertical({query, target, file}, &state->field);
  auto initial_paths = panel_dictionary_paths(host);
  if (initial_paths.empty())
    message(*state, ui_text("no_offline_dictionary_ctrl_d_to"));
  auto render = Renderer(fields, [state, query, target, file] {
    using namespace pawingo::ui;
    Elements content{text(ui_text("search_title")) | color(ink) | bold | hcenter, text("")};
    std::string status;
    std::vector<Vocabulary> results;
    {
      std::lock_guard lock(state->mutex);
      status = state->message;
      results = state->results;
    }
    if (state->page == Page::search) {
      content.push_back(hbox({text("⌕  ") | color(accent),
        query->Render() |
          size(WIDTH, EQUAL, settings().integer("layout.search_query_width", 1, 200)),
        text("  →  ") | color(muted),
        target->Render() | size(WIDTH, EQUAL, settings().integer("layout.target_width", 1, 200))}));
      content.push_back(text(""));
      const int page_size = settings().integer("dictionary.page_size", 1, 10);
      const int start = state->selected / page_size * page_size;
      for (int row = 0; row < page_size; ++row) {
        const int index = start + row;
        if (index >= static_cast<int>(results.size())) {
          content.push_back(text(""));
          continue;
        }
        const auto &word = results[index];
        const bool selected = index == state->selected;
        content.push_back(hbox({text(selection(selected)) | color(accent),
          text(word.expression) | color(selected ? accent : ink) |
            size(WIDTH, EQUAL, settings().integer("layout.expression_width", 1, 200)),
          text(word.reading) | color(secondary) |
            size(WIDTH, EQUAL, settings().integer("layout.reading_width", 1, 200)),
          text(word.meaning) | color(muted) |
            size(WIDTH, EQUAL, settings().integer("layout.meaning_width", 1, 200))}));
      }
      content.push_back(text(""));
      if (!results.empty() && state->selected < static_cast<int>(results.size())) {
        const auto &word = results[state->selected];
        content.push_back(paragraph(word.meaning) | color(ink) |
          size(HEIGHT, EQUAL, settings().integer("layout.status_height", 1, 10)));
        content.push_back(text(word.source + "  ·  " + word.meaning_language) | color(muted));
      } else {
        content.push_back(text("") |
          size(HEIGHT, EQUAL, settings().integer("layout.status_height", 1, 10)));
        content.push_back(text(""));
      }
    } else {
      content.push_back(text(ui_text("jmdict_edrdg")) | color(secondary));
      content.push_back(text(""));
      if (state->page == Page::dictionary) {
        content.push_back(text(std::filesystem::is_regular_file(dictionary_path())
                              ? ui_text("offline_dictionary_installed")
                              : ui_text("no_managed_dictionary_installed")) |
          color(muted));
        content.push_back(text(""));
        for (int i = 0; i < 2; ++i)
          content.push_back(hbox({text(selection(state->resource_choice == i)) | color(accent),
            text(i == 0 ? ui_text("download_update_jmdict") : ui_text("import_local_jmdict")) |
              color(state->resource_choice == i ? ink : secondary)}));
        content.push_back(text(""));
        content.push_back(text(ui_text("www_edrdg_org_dictionary_sources_and")) | color(muted));
      } else if (state->page == Page::confirm) {
        content.push_back(paragraph(ui_text("download_the_multilingual_dictionary_from_www")) |
          color(ink));
        content.push_back(text(""));
        content.push_back(text(ui_text("jmdict_japanese_english_cc_by_sa")) | color(muted));
        content.push_back(text(ui_text("other_translations_retain_their_contributors_licenses")) |
          color(muted));
      } else {
        content.push_back(text(ui_text("jmdict_xml_or_gzip_file")) | color(secondary));
        content.push_back(text(""));
        content.push_back(file->Render());
      }
      content.push_back(text("") |
        size(HEIGHT, EQUAL, settings().integer("layout.dictionary_spacer_height", 0, 10)));
    }
    content.push_back(paragraph(status) | color(muted) |
      size(HEIGHT, EQUAL, settings().integer("layout.search_status_height", 1, 10)));
    return vbox(std::move(content)) | size(WIDTH, EQUAL, content_width);
  });
  auto perform_search = [state, dictionary, &host] {
    if (state->job.running())
      return;
    state->selected = 0;
    const auto query = state->query, target = state->target;
    auto paths = panel_dictionary_paths(host);
    if (query.find_first_not_of(" \t\n\r") == std::string::npos) {
      publish(*state, {}, "");
      return;
    }
    if (paths.empty()) {
      publish(*state, {}, ui_text("no_offline_dictionary_ctrl_d_to"));
      return;
    }
    message(*state, ui_text("searching"));
    launch(*state, [state = state.get(), dictionary, query, target, paths](std::stop_token stop) {
      auto results = dictionary->search(query, target, paths, stop);
      const auto status = results.empty()
        ? ui_text("no_matches_in") + target + ui_text("try_another_word_or_target_language")
        : "";
      publish(*state, std::move(results), status);
    });
  };
  auto component =
    CatchEvent(render, [state, dictionary, &host, speech, fields, perform_search](Event event) {
      if (event == Event::Custom)
        return true;
      if (event == Event::Escape) {
        if (state->page == Page::search)
          return false;
        state->page = state->page == Page::dictionary ? Page::search : Page::dictionary;
        state->field = 0;
        message(*state, "");
        return true;
      }
      if (event == Event::CtrlD) {
        state->page = Page::dictionary;
        state->field = 0;
        return true;
      }
      if (state->page == Page::dictionary) {
        if (event == Event::ArrowUp || event == Event::ArrowDown) {
          state->resource_choice = 1 - state->resource_choice;
          return true;
        }
        if (event == Event::Return && !state->job.running()) {
          state->page = state->resource_choice == 0 ? Page::confirm : Page::import;
          state->field = state->page == Page::import ? 2 : 0;
          message(*state, "");
          return true;
        }
        return true;
      }
      if (state->page == Page::confirm || state->page == Page::import) {
        if (event == Event::Return && !state->job.running()) {
          const bool download = state->page == Page::confirm;
          const auto path = state->file;
          if (!download && path.empty()) {
            message(*state, ui_text("enter_a_local_jmdict_file_path"));
            return true;
          }
          state->page = Page::search;
          state->field = 0;
          state->selected = 0;
          publish(*state, {}, download ? ui_text("downloading_jmdict") : ui_text("reading_jmdict"));
          const auto query = state->query, target = state->target;
          launch(*state,
            [state = state.get(), dictionary, path, download, query, target](std::stop_token stop) {
              if (download)
                fetch(*dictionary, *state, stop);
              else
                dictionary->install(path, stop);
              auto results = dictionary->search(query, target, {dictionary_path()}, stop);
              publish(*state, std::move(results), ui_text("offline_dictionary_ready"));
            });
          return true;
        }
        if (state->page == Page::import)
          return fields->OnEvent(event);
        return true;
      }
      if (event == Event::Tab || event == Event::TabReverse) {
        state->field = 1 - state->field;
        return true;
      }
      if (event == Event::Return) {
        perform_search();
        return true;
      }
      if (event == Event::ArrowDown || event == Event::ArrowUp) {
        std::lock_guard lock(state->mutex);
        if (!state->results.empty())
          state->selected = std::clamp(state->selected + (event == Event::ArrowDown ? 1 : -1),
            0,
            static_cast<int>(state->results.size()) - 1);
        return true;
      }
      if (event == Event::CtrlS || event == Event::CtrlP) {
        Vocabulary word;
        {
          std::lock_guard lock(state->mutex);
          if (state->results.empty() || state->selected >= static_cast<int>(state->results.size()))
            return true;
          word = state->results[state->selected];
        }
        if (event == Event::CtrlS) {
          try {
            host.save(word);
            message(*state, ui_text("saved_for_review"));
          } catch (const std::exception &e) {
            message(*state, e.what());
          }
        } else if (!state->job.running()) {
          message(*state, ui_text("playing"));
          launch(*state, [state = state.get(), speech, text = word.reading](std::stop_token stop) {
            message(*state, speech(text, stop));
          });
        }
        return true;
      }
      return fields->OnEvent(event);
    });
  return {component, [weak = std::weak_ptr<SearchState>(state)] {
            auto state = weak.lock();
            if (!state)
              return std::string{};
            if (state->page == Page::confirm)
              return std::string(ui_text("enter_download"));
            if (state->page == Page::import)
              return std::string(ui_text("enter_import"));
            if (state->page == Page::dictionary)
              return std::string(ui_text("select_enter_open"));
            return std::string(ui_text("enter_find_select_tab_target_s"));
          }};
}
} // namespace pawingo::japanese
