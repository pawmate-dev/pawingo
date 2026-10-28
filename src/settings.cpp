module;
#include <yaml-cpp/yaml.h>

module pawingo.config;

import std;
import pawingo.assets.application;

namespace pawingo::config {
struct Settings::Impl {
  const YAML::Node root;
  const std::string name;

  YAML::Node find(std::string_view path) const {
    YAML::Node node = root;
    while (!path.empty()) {
      auto dot = path.find('.');
      auto key = std::string(path.substr(0, dot));
      const YAML::Node &current = node;
      node.reset(current[key]);
      if (!node.IsDefined())
        throw std::runtime_error(name + ": missing setting " + key);
      if (dot == std::string_view::npos)
        break;
      path.remove_prefix(dot + 1);
    }
    return node;
  }

  template <class T> T get(std::string_view path) const {
    try {
      return find(path).as<T>();
    } catch (const std::exception &e) {
      throw std::runtime_error(name + " [" + std::string(path) + "]: " + e.what());
    }
  }
};

Settings::Settings(std::string_view yaml, std::string name) {
  try {
    impl_ = std::make_shared<const Impl>(YAML::Load(std::string(yaml)), std::move(name));
    if (impl_->get<int>("schema") != 1)
      throw std::runtime_error("Unsupported settings schema.");
  } catch (const std::exception &e) {
    throw std::runtime_error("Invalid embedded settings: " + std::string(e.what()));
  }
}

std::string Settings::text(std::string_view p) const {
  return impl_->get<std::string>(p);
}

int Settings::integer(std::string_view p, int min, int max) const {
  auto v = impl_->get<int>(p);
  if (v < min || v > max)
    throw std::runtime_error(impl_->name + ": setting out of range: " + std::string(p));
  return v;
}

bool Settings::flag(std::string_view p) const {
  return impl_->get<bool>(p);
}

float Settings::real(std::string_view p, float min, float max) const {
  const auto value = impl_->get<float>(p);
  if (!std::isfinite(value) || value < min || value > max)
    throw std::runtime_error(impl_->name + ": setting out of range: " + std::string(p));
  return value;
}

std::vector<std::string> Settings::strings(std::string_view p) const {
  return impl_->get<std::vector<std::string>>(p);
}

std::vector<int> Settings::integers(std::string_view p) const {
  return impl_->get<std::vector<int>>(p);
}

std::vector<std::vector<std::string>> Settings::rows(std::string_view p) const {
  return impl_->get<std::vector<std::vector<std::string>>>(p);
}

std::map<std::string, std::string> Settings::mapping(std::string_view p) const {
  return impl_->get<std::map<std::string, std::string>>(p);
}

const Settings &application() {
  static const Settings value(assets::application, "application.yaml");
  return value;
}

std::string ui_text(std::string_view key) {
  return application().text("messages." + std::string(key));
}

namespace {
std::filesystem::path base(bool preferences) {
  if (auto p = std::getenv(preferences ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME"); p && *p)
    return p;
#ifdef _WIN32
  if (auto p = std::getenv(preferences ? "APPDATA" : "LOCALAPPDATA"); p && *p)
    return p;
#endif
  if (auto p = std::getenv("HOME"); p && *p)
    return std::filesystem::path(p) / (preferences ? ".config" : ".local/share");
  throw std::runtime_error("No user configuration/data directory is available.");
}
} // namespace

std::filesystem::path data_path(const std::filesystem::path &relative) {
  return base(false) / application().text("storage.directory") / relative;
}

std::filesystem::path preferences_path(const std::filesystem::path &relative) {
  return base(true) / application().text("storage.directory") / relative;
}
} // namespace pawingo::config
