module;
// Cross-library dynamic_cast requires visible type information, including named-module types.
#if defined(__clang__) && !defined(_WIN32)
#define PAWINGO_INTERFACE __attribute__((type_visibility("default")))
#else
#define PAWINGO_INTERFACE
#endif

export module pawingo.host;

import std;
export import pawingo.types;

/// Frontend-neutral services supplied to independently loaded language backends.
export namespace pawingo {
/// Optional interfaces requested by plugins; returned pointers are borrowed from the host.
/// ui_refresh returns UiRefresh*, review returns ui::ReviewProvider* (pawingo.review_ui).
enum class HostCapability { ui_refresh, review };

/// Services are provided by the host and remain valid until the plugin is destroyed.
class PAWINGO_INTERFACE Host {
public:
  virtual ~Host() = default;

  /// Returns the requested optional interface or nullptr; valid until host destruction.
  virtual void *capability(HostCapability) {
    return nullptr;
  }

  /// Saves a sourced vocabulary entry; validation and persistence failures propagate.
  virtual void save(const Vocabulary &entry) = 0;
  /// Returns configured resources, including disabled entries, in provider priority order.
  virtual std::vector<Resource> resources(ResourceKind kind, const std::string &language) = 0;
};

/// Optional UI service. A plugin must post redraws through the host rather than
/// relying on FTXUI globals that may be duplicated in a dynamically loaded library.
class PAWINGO_INTERFACE UiRefresh {
public:
  virtual ~UiRefresh() = default;
  /// Schedules a frontend redraw; may be called from a plugin worker.
  virtual void request_redraw() = 0;
};

} // namespace pawingo
