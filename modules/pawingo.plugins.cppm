export module pawingo.plugins;

import std;
export import pawingo.plugin;

/// Registry ownership and discovery for native language plugins.
export namespace pawingo {

/// Owns loaded libraries and plugin instances, destroying instances before closing libraries.
class Plugins {
public:
  Plugins();
  ~Plugins();
  Plugins(const Plugins &) = delete;
  Plugins &operator=(const Plugins &) = delete;
  /// Loads one library after validating its descriptor, ABI tag, and unique ID.
  ///
  /// @param path Native shared-library file to open.
  /// @throws std::runtime_error If loading, descriptor validation, or creation fails.
  ///
  /// Library initializers and the entry point execute before validation. Only trusted plugins
  /// should be loaded. All borrowed plugin pointers and panels must be released before teardown.
  void load(const std::string &path);
  /// Loads sorted shared-library candidates, retaining errors and continuing after failures.
  void discover(const std::string &directory);
  /// Pointers are borrowed and remain valid only while this registry is alive.
  std::vector<LanguagePlugin *> languages() const;
  /// Returns discovery diagnostics retained for this registry lifetime.
  const std::vector<std::string> &errors() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace pawingo
