module;
// Cross-library dynamic_cast requires visible type information, including named-module types.
#if defined(__clang__) && !defined(_WIN32)
#define PAWINGO_INTERFACE __attribute__((type_visibility("default")))
#else
#define PAWINGO_INTERFACE
#endif
#include <ftxui/component/component.hpp>

export module pawingo.plugin;

import std;
export import pawingo.build;
export import pawingo.host;

/// Plugins and the host must use the same C++ ABI, standard library and FTXUI.
export namespace pawingo {
inline constexpr std::uint32_t plugin_api_version = 1;
inline constexpr const char *plugin_entry_symbol = "pawingo_plugin_v1";

class PanelHelpProvider;

/// Language-specific capabilities exposed through the native plugin boundary.
class PAWINGO_INTERFACE LanguagePlugin {
public:
  virtual ~LanguagePlugin() = default;

  /// Returns optional footer help, borrowed until plugin destruction.
  virtual PanelHelpProvider *panel_help_provider() {
    return nullptr;
  }

  /// Returns the language's display name.
  virtual std::string language() const = 0;
  /// Empty means no result. Every returned entry must identify its source;
  /// meaning_language selects the language in which meanings are requested.
  virtual std::vector<Vocabulary> search(const std::string &query,
    const std::string &meaning_language) = 0;
  /// Assesses an answer using the language backend's matching rules.
  virtual MatchResult match(const Vocabulary &entry, const std::string &answer) = 0;
  /// Panel IDs must be stable and unique within this plugin. The caller must
  /// destroy every returned component before destroying or unloading the plugin.
  virtual std::vector<std::string> panels() const = 0;
  /// Creates a panel for a declared ID, borrowing host until panel destruction.
  /// Returns nullptr for unsupported IDs; workers must finish before the panel is released.
  virtual ftxui::Component create_panel(const std::string &id, Host &host) = 0;
};

/// Optional extension: the host places panel shortcuts in its screen footer.
/// Accessed through panel_help_provider(), without cross-library RTTI.
class PAWINGO_INTERFACE PanelHelpProvider {
public:
  virtual ~PanelHelpProvider() = default;
  /// Returns the complete footer, including current Escape behavior, without changing panel state.
  virtual std::string panel_help(const std::string &id) const = 0;
};

/// The entry point returns a descriptor that remains valid until the native library is unloaded.
/// Its ABI fields are checked before invoking the create function. Loading the
/// library and calling its entry point can already execute plugin code.
struct PluginDescriptor {
  std::uint32_t api_version;
  const char *abi_tag;
  const char *id;
  const char *name;
  LanguagePlugin *(*create)();
  void (*destroy)(LanguagePlugin *);
};

using PluginEntry = const PluginDescriptor *(*)();

/// This tag is a necessary compatibility check, not a substitute for using the
/// same compiler, standard library, build flags and FTXUI installation.
inline constexpr const char *plugin_abi_tag = build::abi;
} // namespace pawingo

/// Export exactly this C symbol from a plugin:
/// extern "C" const pawingo::PluginDescriptor *pawingo_plugin_v1();
