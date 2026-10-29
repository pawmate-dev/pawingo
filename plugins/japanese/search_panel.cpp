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
import pawingo.japanese.examples;

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
enum class Page { search, detail, dictionary, confirm, import };

struct SearchState {
  std::string query, target = settings().text("dictionary.target"), file;
  Page page = Page::search;
  Page dictionary_return = Page::search;
  std::optional<Vocabulary> detail;
  std::vector<Example> examples;
  std::string examples_status;
  bool examples_loading = false;
  std::string searched_query, searched_target;
  // Zero selects the word; subsequent positions select Japanese example sentences.
  int speech_selection = 0;
  bool follow_selection = true;
  float detail_scroll = 0.F;
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
      {
        std::lock_guard lock(state.mutex);
        if (state.examples_loading) {
          state.examples_loading = false;
          state.examples_status = ui_text("examples_unavailable");
        }
      }
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

void fetch_examples(Examples &examples, SearchState &state, std::stop_token stop) {
  pawingo::net::TemporaryDownload temporary(examples.path().parent_path());
  auto result = pawingo::net::download_https(
    {.url = settings().text("examples.url"),
      .destination = temporary.file(),
      .max_bytes =
        static_cast<std::size_t>(settings().integer("examples.download_bytes", 1, 1073741824)),
      .timeout = std::chrono::seconds(settings().integer("examples.timeout_seconds", 1, 3600))},
    stop
  );
  if (!result) {
    if (stop.stop_requested())
      return;
    throw std::runtime_error(result.error().message);
  }
  message(state, ui_text("importing_examples"));
  auto installed = examples.install_index(temporary.file(), stop);
  if (!installed && installed.error().kind != ExampleErrorKind::cancelled)
    throw std::runtime_error(installed.error().message);
}

// Keep corpus and dictionary work off the input thread. Captured state is owned by its joined job.
void detail_examples(SearchState &state,
  Dictionary &dictionary,
  Examples &examples,
  const Vocabulary &word,
  const std::vector<std::filesystem::path> &paths,
  std::stop_token stop) {
  std::vector<Example> matches;
  std::string status;
  try {
    if (!std::filesystem::is_regular_file(examples.path())) {
      status = ui_text("examples_not_installed");
    } else if (auto reference = dictionary.reference(word, paths, stop)) {
      auto found =
        examples.find({*reference, word.expression, word.reading, word.meaning_language}, stop);
      if (!found) {
        if (found.error().kind == ExampleErrorKind::cancelled)
          return;
        status = found.error().message;
      } else {
        matches = std::move(*found);
        if (matches.empty())
          status = ui_text("no_linked_examples");
      }
    } else
      status = ui_text("no_linked_examples");
  } catch (const std::exception &e) {
    if (stop.stop_requested())
      return;
    status = e.what();
  }
  if (stop.stop_requested())
    return;
  std::lock_guard lock(state.mutex);
  state.examples = std::move(matches);
  state.examples_status = std::move(status);
  state.examples_loading = false;
}
} // namespace

SearchPanel search_panel(std::shared_ptr<Dictionary> dictionary,
  Host &host,
  Speech speech,
  std::shared_ptr<Examples> examples) {
  using namespace ftxui;
  using namespace pawingo::ui;
  auto state = std::make_shared<SearchState>();
  if (!examples)
    examples = std::make_shared<Examples>();
  if (auto *events = dynamic_cast<pawingo::UiRefresh *>(&host))
    state->redraw = [events] {
      events->request_redraw();
    };
  InputOption options;
  options.multiline = false;
  options.transform = [](InputState input) {
    return input.element | color(input.is_placeholder ? muted : input.focused ? ink : secondary);
  };
  auto query = pawingo::ui::text_input(&state->query, ui_text("word_reading_or_meaning"), options);
  auto target =
    pawingo::ui::text_input(&state->target, settings().text("dictionary.target"), options);
  auto file = pawingo::ui::text_input(&state->file, ui_text("resource_file_path"), options);
  auto fields = Container::Vertical({query, target, file}, &state->field);
  auto initial_paths = panel_dictionary_paths(host);
  if (initial_paths.empty())
    message(*state, ui_text("no_offline_dictionary_ctrl_d_to"));
  auto render = Renderer(fields, [state, query, target, file, corpus = examples] {
    using namespace pawingo::ui;
    Elements content{text(state->page == Page::detail && state->detail ? state->detail->expression
                                                                       : ui_text("search_title")) |
        color(ink) | bold | hcenter,
      text("")};
    std::string status;
    std::string examples_status;
    std::vector<Example> examples;
    std::vector<Vocabulary> results;
    {
      std::lock_guard lock(state->mutex);
      status = state->message;
      results = state->results;
      examples = state->examples;
      examples_status = state->examples_status;
    }
    if (state->page == Page::detail && state->detail) {
      const bool selected = state->speech_selection == 0;
      content[0] = hbox({text(selection(selected)) | color(accent),
                     text(state->detail->expression) | color(selected ? accent : ink),
                     text(selection(false))}) |
        hcenter;
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
    } else if (state->page == Page::detail && state->detail) {
      const auto &word = *state->detail;
      content.push_back(text(word.reading) | color(secondary) | hcenter);
      content.push_back(text(""));
      auto meaning = paragraph(word.meaning) | color(ink);
      if (state->follow_selection && state->speech_selection == 0)
        meaning = meaning | focus;
      Elements body{meaning,
        text(""),
        paragraph(word.source + "  ·  " + word.meaning_language) | color(muted)};
      if (!word.context.empty()) {
        body.push_back(text(""));
        body.push_back(paragraph(word.context) | color(secondary));
      }
      body.push_back(text(""));
      body.push_back(text(ui_text("examples_title")) | color(secondary));
      if (!examples_status.empty())
        body.push_back(paragraph(examples_status) | color(muted));
      for (std::size_t i = 0; i < examples.size(); ++i) {
        const auto &example = examples[i];
        const bool selected = state->speech_selection == static_cast<int>(i + 1);
        body.push_back(text(""));
        auto sentence = paragraph(example.text) | color(selected ? accent : ink);
        if (selected && state->follow_selection)
          sentence = sentence | focus;
        Elements lines{sentence,
          paragraph(example.translation.empty()
              ? ui_text("no_example_translation") + example.translation_language
              : example.translation) |
            color(secondary)};
        Elements source{text(ui_text("example_source") + std::to_string(example.sentence_id)) |
          hyperlink(example.source_url) | color(muted)};
        if (example.translation_id)
          source.push_back(text("  ·  #" + std::to_string(example.translation_id)) |
            hyperlink(example.translation_url) | color(muted));
        if (!example.sense_link)
          source.push_back(text(ui_text("example_word_level")) | color(muted));
        lines.push_back(hbox(std::move(source)));
        // Keep wrapped sentences, translations and provenance in one column beside the marker.
        body.push_back(hbox({text(selection(selected)) | color(accent),
          vbox(std::move(lines)) | flex}));
      }
      if (!examples.empty()) {
        body.push_back(text(""));
        body.push_back(paragraph(examples.front().attribution + "  ·  " +
                         examples.front().license) |
          color(muted));
      }
      auto detail = vbox(std::move(body));
      if (!state->follow_selection)
        detail = detail | focusPositionRelative(0.F, state->detail_scroll);
      content.push_back(detail | yframe |
        size(HEIGHT, EQUAL, settings().integer("layout.detail_height", 1, 30)));
    } else {
      content.push_back(text(ui_text("resource_title")) | color(secondary));
      content.push_back(text(""));
      if (state->page == Page::dictionary) {
        content.push_back(text(std::filesystem::is_regular_file(dictionary_path())
                              ? ui_text("offline_dictionary_installed")
                              : ui_text("no_managed_dictionary_installed")) |
          color(muted));
        content.push_back(text(std::filesystem::is_regular_file(corpus->path())
                              ? ui_text("examples_installed")
                              : ui_text("examples_not_installed")) |
          color(muted));
        content.push_back(text(""));
        const std::array labels{ui_text("download_update_jmdict"),
          ui_text("import_local_jmdict"),
          ui_text("download_examples"),
          ui_text("import_example_index"),
          ui_text("import_example_translations")};
        for (int i = 0; i < static_cast<int>(labels.size()); ++i)
          content.push_back(hbox({text(selection(state->resource_choice == i)) | color(accent),
            text(labels[i]) | color(state->resource_choice == i ? ink : secondary)}));
        content.push_back(text(""));
        content.push_back(text(ui_text("www_edrdg_org_dictionary_sources_and")) | color(muted));
      } else if (state->page == Page::confirm) {
        content.push_back(paragraph(ui_text(state->resource_choice == 2
                              ? "confirm_example_download"
                              : "download_the_multilingual_dictionary_from_www")) |
          color(ink));
        content.push_back(text(""));
        content.push_back(text(ui_text(state->resource_choice == 2
                              ? "example_license"
                              : "jmdict_japanese_english_cc_by_sa")) |
          color(muted));
        content.push_back(text(ui_text(state->resource_choice == 2
                              ? "example_download_languages"
                              : "other_translations_retain_their_contributors_licenses")) |
          color(muted));
      } else {
        content.push_back(paragraph(state->resource_choice == 1 ? ui_text("jmdict_xml_or_gzip_file")
                              : state->resource_choice == 3
                              ? ui_text("example_index_format")
                              : ui_text("example_pair_format") + gloss_language(state->target)) |
          color(secondary));
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
    state->searched_query = query;
    state->searched_target = target;
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
  auto component = CatchEvent(render,
    [state, dictionary, examples, &host, speech, fields, perform_search](Event event) {
      if (event == Event::Custom)
        return true;
      if (event == Event::Escape) {
        if (state->page == Page::search)
          return false;
        if (state->page == Page::detail) {
          state->job.request_stop();
          state->page = Page::search;
          state->detail.reset();
        } else {
          state->page =
            state->page == Page::dictionary ? state->dictionary_return : Page::dictionary;
        }
        state->field = 0;
        message(*state, "");
        return true;
      }
      if (event == Event::CtrlD && (state->page == Page::search || state->page == Page::detail)) {
        state->dictionary_return = state->page;
        state->page = Page::dictionary;
        state->field = 0;
        return true;
      }
      if (state->page == Page::dictionary) {
        if (event == Event::ArrowUp || event == Event::ArrowDown) {
          state->resource_choice =
            std::clamp(state->resource_choice + (event == Event::ArrowDown ? 1 : -1), 0, 4);
          return true;
        }
        if (event == Event::Return && !state->job.running()) {
          state->page = state->resource_choice == 0 || state->resource_choice == 2 ? Page::confirm
                                                                                   : Page::import;
          state->field = state->page == Page::import ? 2 : 0;
          message(*state, "");
          return true;
        }
        return true;
      }
      if (state->page == Page::confirm || state->page == Page::import) {
        if (event == Event::Return && !state->job.running()) {
          const int action = state->resource_choice;
          const bool download = action == 0 || action == 2;
          const auto path = state->file;
          if (!download && path.empty()) {
            message(*state, ui_text("enter_resource_file_path"));
            return true;
          }
          const auto detail = action >= 2 && state->dictionary_return == Page::detail
            ? state->detail
            : std::optional<Vocabulary>{};
          const auto paths = panel_dictionary_paths(host);
          state->page = detail ? Page::detail : Page::search;
          if (!detail)
            state->detail.reset();
          state->field = 0;
          state->selected = 0;
          const auto progress = action == 0 ? ui_text("downloading_jmdict")
            : action == 1                   ? ui_text("reading_jmdict")
            : download                      ? ui_text("downloading_examples")
                                            : ui_text("importing_examples");
          if (!detail)
            publish(*state, {}, progress);
          else {
            std::lock_guard lock(state->mutex);
            state->message = progress;
            state->examples.clear();
            state->speech_selection = 0;
            state->follow_selection = true;
            state->examples_status = ui_text("loading_examples");
            state->examples_loading = true;
          }
          const auto query = state->query, target = state->target;
          state->searched_query = query;
          state->searched_target = target;
          launch(*state,
            [state = state.get(), dictionary, examples, path, action, query, target, detail, paths](
              std::stop_token stop
            ) {
              if (action == 0)
                fetch(*dictionary, *state, stop);
              else if (action == 1)
                dictionary->install(path, stop);
              else if (action == 2)
                fetch_examples(*examples, *state, stop);
              else {
                auto installed = action == 3 ? examples->install_index(path, stop)
                                             : examples->install_translations({path, target}, stop);
                if (!installed) {
                  if (installed.error().kind == ExampleErrorKind::cancelled)
                    return;
                  throw std::runtime_error(installed.error().message);
                }
              }
              if (stop.stop_requested())
                return;
              if (detail) {
                detail_examples(*state, *dictionary, *examples, *detail, paths, stop);
                message(*state, ui_text("example_resource_ready"));
              } else {
                auto results = dictionary->search(query,
                  target,
                  action < 2 ? std::vector<std::filesystem::path>{dictionary_path()} : paths,
                  stop);
                publish(*state,
                  std::move(results),
                  action < 2 ? ui_text("offline_dictionary_ready")
                             : ui_text("example_resource_ready"));
              }
            });
          return true;
        }
        if (state->page == Page::import)
          return fields->OnEvent(event);
        return true;
      }
      if (state->page == Page::detail && (event == Event::Tab || event == Event::TabReverse)) {
        std::lock_guard lock(state->mutex);
        const int count = static_cast<int>(state->examples.size()) + 1;
        state->speech_selection =
          (state->speech_selection + (event == Event::Tab ? 1 : count - 1)) % count;
        state->follow_selection = true;
        return true;
      }
      if (state->page == Page::search && (event == Event::Tab || event == Event::TabReverse)) {
        state->field = 1 - state->field;
        return true;
      }
      if (event == Event::Return) {
        if (state->page == Page::detail || state->job.running())
          return true;
        std::optional<Vocabulary> detail;
        {
          std::lock_guard lock(state->mutex);
          if (state->query == state->searched_query && state->target == state->searched_target &&
            !state->results.empty() && state->selected < static_cast<int>(state->results.size())) {
            state->detail = state->results[state->selected];
            state->detail_scroll = 0.F;
            state->page = Page::detail;
            state->message.clear();
            state->examples.clear();
            state->speech_selection = 0;
            state->follow_selection = true;
            state->examples_status = ui_text("loading_examples");
            state->examples_loading = true;
            detail = state->detail;
          }
        }
        if (detail) {
          const auto paths = panel_dictionary_paths(host);
          launch(*state,
            [state = state.get(), dictionary, examples, word = *detail, paths](
              std::stop_token stop
            ) {
              detail_examples(*state, *dictionary, *examples, word, paths, stop);
            });
          return true;
        }
        perform_search();
        return true;
      }
      if (event == Event::ArrowDown || event == Event::ArrowUp) {
        if (state->page == Page::detail) {
          state->follow_selection = false;
          const float step =
            settings().integer("layout.detail_scroll_step_percent", 1, 100) / 100.F;
          state->detail_scroll =
            std::clamp(state->detail_scroll + (event == Event::ArrowDown ? step : -step), 0.F, 1.F);
          return true;
        }
        std::lock_guard lock(state->mutex);
        if (!state->results.empty())
          state->selected = std::clamp(state->selected + (event == Event::ArrowDown ? 1 : -1),
            0,
            static_cast<int>(state->results.size()) - 1);
        return true;
      }
      if (event == Event::CtrlS || event == Event::CtrlP) {
        Vocabulary word;
        SpeechRequest request;
        {
          std::lock_guard lock(state->mutex);
          if (state->results.empty() || state->selected >= static_cast<int>(state->results.size()))
            return true;
          word = state->page == Page::detail && state->detail ? *state->detail
                                                              : state->results[state->selected];
          request = {.text = word.expression, .reading = word.reading};
          if (state->page == Page::detail && state->speech_selection > 0 &&
            state->speech_selection <= static_cast<int>(state->examples.size()))
            request = {.text = state->examples[state->speech_selection - 1].text};
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
          launch(*state,
            [state = state.get(), speech, request = std::move(request)](std::stop_token stop) {
              message(*state, speech(request, stop));
            });
        }
        return true;
      }
      return state->page == Page::detail ? true : fields->OnEvent(event);
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
            if (state->page == Page::detail)
              return std::string(ui_text("detail_shortcuts"));
            {
              std::lock_guard lock(state->mutex);
              if (!state->results.empty() && state->query == state->searched_query &&
                state->target == state->searched_target)
                return std::string(ui_text("enter_open_select_tab_target_s"));
            }
            return std::string(ui_text("enter_find_select_tab_target_s"));
          }};
}
} // namespace pawingo::japanese
