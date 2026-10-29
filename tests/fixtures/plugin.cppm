module;
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

export module pawingo.tests.fixture;

import std;
export import pawingo.plugin;

namespace {
class Fixture : public pawingo::LanguagePlugin {
public:
  std::string language() const override {
    return "fixture";
  }

  std::vector<pawingo::Vocabulary> search(const std::string &, const std::string &) override {
    return {};
  }

  pawingo::MatchResult match(const pawingo::Vocabulary &, const std::string &) override {
    return {};
  }

  std::vector<std::string> panels() const override {
    return {"example"};
  }

  ftxui::Component create_panel(const std::string &, pawingo::Host &) override {
    return ftxui::Renderer([] {
      return ftxui::text("Fixture panel");
    });
  }
};

pawingo::LanguagePlugin *create() {
  return new Fixture;
}

void destroy(pawingo::LanguagePlugin *p) {
  delete p;
}

const pawingo::PluginDescriptor descriptor{pawingo::plugin_api_version,
  pawingo::plugin_abi_tag,
  "fixture",
  "Fixture",
  create,
  destroy};
} // namespace

export const pawingo::PluginDescriptor *fixture_descriptor() {
  return &descriptor;
}
