export module pawingo.config;

import std;

export namespace pawingo::config {
/// Immutable embedded YAML document. Conversion and validation failures throw std::runtime_error
/// with the document/field name. YAML library objects never cross this frontend-neutral boundary.
class Settings {
public:
  Settings(std::string_view yaml, std::string name);
  std::string text(std::string_view path) const;
  int integer(std::string_view path, int minimum, int maximum) const;
  /// Reads a finite floating-point setting within inclusive bounds.
  float real(std::string_view path, float minimum, float maximum) const;
  bool flag(std::string_view path) const;
  std::vector<std::string> strings(std::string_view path) const;
  std::vector<int> integers(std::string_view path) const;
  std::vector<std::vector<std::string>> rows(std::string_view path) const;
  std::map<std::string, std::string> mapping(std::string_view path) const;

private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};

/// Shared application defaults embedded at build time; never reads the working directory.
const Settings &application();
/// Returns application presentation copy from the embedded catalog.
std::string ui_text(std::string_view key);
/// Resolves an application-relative path through native user-data/config directory conventions.
std::filesystem::path data_path(const std::filesystem::path &relative);
std::filesystem::path preferences_path(const std::filesystem::path &relative);
} // namespace pawingo::config
