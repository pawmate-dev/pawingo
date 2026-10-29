module;
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

module pawingo.packages_ui;

import std;
import pawingo.ui;
import pawingo.tasks;

namespace pawingo::ui {
namespace {
using config::ui_text;
enum class Phase { list, confirm, busy };

struct State {
  std::shared_ptr<packages::Manager> manager;
  std::function<void()> redraw;
  std::mutex mutex;
  std::vector<packages::Offer> offers;
  std::string status;
  Phase phase = Phase::list;
  int selected = 0;
  bool refresh = false;
  tasks::BackgroundJob job;
};

std::string label(packages::OfferState state) {
  switch (state) {
  case packages::OfferState::available:
    return ui_text("packages_available");
  case packages::OfferState::installed:
    return ui_text("packages_installed");
  case packages::OfferState::update:
    return ui_text("packages_update");
  case packages::OfferState::incompatible:
    return ui_text("packages_incompatible");
  default:
    return ui_text("packages_unpublished");
  }
}

void start(State &state) {
  std::optional<packages::Offer> offer;
  {
    std::lock_guard lock(state.mutex);
    if (!state.refresh)
      offer = state.offers[state.selected];
    state.phase = Phase::busy;
    state.status = ui_text("packages_working");
  }
  state.job.start(
    [&state, offer](std::stop_token stop) {
      auto result = offer ? state.manager->install(*offer, stop) : state.manager->refresh(stop);
      auto offers = state.manager->offers();
      std::lock_guard lock(state.mutex);
      if (offers) {
        state.offers = std::move(*offers);
        state.selected =
          std::clamp(state.selected, 0, std::max(0, static_cast<int>(state.offers.size()) - 1));
      }
      state.status = result ? ui_text(offer ? "packages_pending" : "packages_refresh_done")
        : result.error().kind == packages::ErrorKind::cancelled ? ui_text("packages_cancelled")
                                                                : result.error().message;
    },
    [&state](std::exception_ptr error) {
      {
        std::lock_guard lock(state.mutex);
        state.phase = Phase::list;
        if (error)
          state.status = ui_text("packages_operation_failed");
      }
      if (state.redraw)
        state.redraw();
    }
  );
}
} // namespace

PackagePanel package_panel(std::shared_ptr<packages::Manager> manager,
  Store &store,
  std::function<void()> redraw) {
  using namespace ftxui;
  auto state = std::make_shared<State>(std::move(manager), std::move(redraw));
  if (auto offers = state->manager->offers())
    state->offers = std::move(*offers);
  else
    state->status = offers.error().message;
  auto render = Renderer([state] {
    std::lock_guard lock(state->mutex);
    const auto &settings = config::application();
    Elements content{text(ui_text("get_plugins")) | color(ink) | hcenter, text("")};
    const int rows = settings.integer("packages.list_rows", 1, 10);
    const int first = state->selected / rows * rows;
    for (int row = 0; row < rows; ++row) {
      const int index = first + row;
      if (index >= static_cast<int>(state->offers.size())) {
        content.push_back(text(""));
        continue;
      }
      const auto &offer = state->offers[index];
      const bool selected = index == state->selected;
      content.push_back(hbox({text(selection(selected)) | color(accent),
        text(offer.package.name) | color(selected ? ink : secondary),
        text("  " + offer.package.version + "  ·  " + label(offer.state)) | color(muted)}));
    }
    content.push_back(text(""));
    std::string detail;
    if (!state->offers.empty()) {
      const auto &offer = state->offers[state->selected];
      detail = offer.package.description;
      if (state->phase == Phase::confirm && !state->refresh)
        detail = ui_text("packages_confirm_install") + offer.package.name + " " +
          offer.package.version + " (" + std::to_string(offer.artifact->size_bytes / 1024) +
          " KiB)?\n" + offer.artifact->url;
    } else
      detail = ui_text("packages_empty");
    if (state->phase == Phase::confirm && state->refresh)
      detail = ui_text("packages_confirm_refresh");
    content.push_back(paragraph(detail) | color(state->phase == Phase::confirm ? ink : secondary) |
      size(HEIGHT, EQUAL, settings.integer("packages.description_height", 1, 6)));
    content.push_back(text(""));
    content.push_back(paragraph(state->status) | color(muted) |
      size(HEIGHT, EQUAL, settings.integer("packages.status_height", 1, 6)));
    return vbox(std::move(content)) | size(WIDTH, EQUAL, content_width);
  });
  auto component = CatchEvent(render, [state, &store](Event event) {
    try {
      Phase phase;
      {
        std::lock_guard lock(state->mutex);
        phase = state->phase;
      }
      if (event == Event::Escape) {
        if (phase == Phase::busy || state->job.running()) {
          state->job.request_stop();
          std::lock_guard lock(state->mutex);
          state->status = ui_text("packages_cancelling");
          return true;
        }
        if (phase == Phase::confirm) {
          std::lock_guard lock(state->mutex);
          state->phase = Phase::list;
          return true;
        }
        return false;
      }
      if (phase == Phase::busy || state->job.running())
        return true;
      if (phase == Phase::confirm) {
        if (event == Event::Return || event == Event::Character('a')) {
          if (event == Event::Character('a'))
            store.set_policy(config::application().text("packages.service"), OnlinePolicy::allow);
          start(*state);
        }
        return true;
      }
      if (event == Event::ArrowUp || event == Event::ArrowDown) {
        std::lock_guard lock(state->mutex);
        state->selected = std::clamp(state->selected + (event == Event::ArrowDown ? 1 : -1),
          0,
          std::max(0, static_cast<int>(state->offers.size()) - 1));
        return true;
      }
      if (event == Event::Character('r') || event == Event::Return) {
        {
          std::lock_guard lock(state->mutex);
          state->refresh = event == Event::Character('r');
          if (!state->refresh &&
            (state->offers.empty() ||
              (state->offers[state->selected].state != packages::OfferState::available &&
                state->offers[state->selected].state != packages::OfferState::update)))
            return true;
        }
        auto policy = store.policy(config::application().text("packages.service"));
        if (policy == OnlinePolicy::offline) {
          std::lock_guard lock(state->mutex);
          state->status = ui_text("packages_offline");
        } else if (policy == OnlinePolicy::allow)
          start(*state);
        else {
          std::lock_guard lock(state->mutex);
          state->phase = Phase::confirm;
        }
        return true;
      }
      return true;
    } catch (const std::exception &error) {
      std::lock_guard lock(state->mutex);
      state->phase = Phase::list;
      state->status = error.what();
      return true;
    } catch (...) {
      std::lock_guard lock(state->mutex);
      state->phase = Phase::list;
      state->status = ui_text("packages_operation_failed");
      return true;
    }
  });
  return {component, [weak = std::weak_ptr<State>(state)] {
            auto state = weak.lock();
            if (!state)
              return std::string{};
            std::lock_guard lock(state->mutex);
            return ui_text(state->phase == Phase::busy ? "packages_busy_help"
                : state->phase == Phase::confirm       ? "packages_confirm_help"
                                                       : "packages_help");
          }};
}
} // namespace pawingo::ui
