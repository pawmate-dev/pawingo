#pragma once

#include <cstdint>
#include <ftxui/component/component.hpp>
#include <string>
#include <vector>

// Plugins and the host must use the same C++ ABI, standard library and FTXUI.
namespace pawingo {
inline constexpr std::uint32_t plugin_api_version = 1;
inline constexpr const char *plugin_entry_symbol = "pawingo_plugin_v1";

struct Vocabulary {
  // Language identifiers are supplied by the plugin; meaning_language identifies
  // the language of the definition, which may differ from the studied language.
  std::string language;
  std::string expression;
  std::string reading;
  std::string meaning_language;
  std::string meaning;
  std::string source;
  std::string context;
  // Assigned by the store; newly constructed entries can leave this at zero.
  std::int64_t id = 0;
};

// "acceptable" lets a plugin distinguish a tolerated variant from an exact answer.
enum class Match { correct, acceptable, incorrect };
struct MatchResult {
  Match result = Match::incorrect;
  std::string feedback;
};

enum class ResourceKind { dictionary, tts, translation };
struct Resource {
  std::string id;
  std::string name;
  ResourceKind kind;
  std::string language;
  std::string location;
  bool enabled = true;
  // Larger priorities appear first in the host's resource list.
  int priority = 0;
};

// Services are provided by the host and remain valid until the plugin is destroyed.
class Host {
public:
  virtual ~Host() = default;
  virtual void save(const Vocabulary &entry) = 0;
  virtual std::vector<Resource> resources(ResourceKind kind, const std::string &language) = 0;
};

class LanguagePlugin {
public:
  virtual ~LanguagePlugin() = default;
  virtual std::string language() const = 0;
  // Empty means no result. Every returned entry must identify its source;
  // meaning_language selects the language in which meanings are requested.
  virtual std::vector<Vocabulary> search(const std::string &query,
    const std::string &meaning_language) = 0;
  virtual MatchResult match(const Vocabulary &entry, const std::string &answer) = 0;
  // Panel IDs must be stable and unique within this plugin. The caller must
  // destroy every returned component before destroying or unloading the plugin.
  virtual std::vector<std::string> panels() const = 0;
  virtual ftxui::Component create_panel(const std::string &id, Host &host) = 0;
};

// Optional extension: the host places panel shortcuts in its screen footer.
// Keeping this separate preserves the existing LanguagePlugin vtable.
class PanelHelpProvider {
public:
  virtual ~PanelHelpProvider() = default;
  virtual std::string panel_help(const std::string &id) const = 0;
};

// The entry point returns a descriptor that remains valid until dlclose.
// Its ABI fields are checked before invoking the create function. Loading the
// library and calling its entry point can already execute plugin code.
struct PluginDescriptor {
  std::uint32_t api_version;
  const char *abi_tag;
  const char *id;
  const char *name;
  LanguagePlugin *(*create)();
  void (*destroy)(LanguagePlugin *);
};
using PluginEntry = const PluginDescriptor *(*)();

// This tag is a necessary compatibility check, not a substitute for using the
// same compiler, standard library, build flags and FTXUI installation.
#if defined(__clang__)
#define PAWINGO_COMPILER_TAG "clang-" __clang_version__
#elif defined(__GNUC__)
#define PAWINGO_STRINGIFY_IMPL(x) #x
#define PAWINGO_STRINGIFY(x) PAWINGO_STRINGIFY_IMPL(x)
#define PAWINGO_COMPILER_TAG \
  "gcc-" PAWINGO_STRINGIFY(__GNUC__) "." PAWINGO_STRINGIFY(__GNUC_MINOR__)
#else
#define PAWINGO_COMPILER_TAG "unknown"
#endif
inline constexpr const char *plugin_abi_tag = PAWINGO_COMPILER_TAG ";ftxui-6.1.9;api-1";
} // namespace pawingo

// Export exactly this C symbol from a plugin:
// extern "C" const pawingo::PluginDescriptor *pawingo_plugin_v1();
