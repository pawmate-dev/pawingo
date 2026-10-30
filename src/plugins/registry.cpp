module pawingo.plugins;

import std;
import pawingo.plugin;
import pawingo.packages;
import pawingo.native;

namespace pawingo {
struct Plugins::Impl {
  struct Loaded {
    native::Library library;
    const PluginDescriptor *descriptor;
    LanguagePlugin *plugin;
  };

  std::vector<Loaded> loaded;
  std::vector<std::string> errors;

  ~Impl() {
    for (auto it = loaded.rbegin(); it != loaded.rend(); ++it) {
      // The destroy function and the object's vtable live in the shared library.
      it->descriptor->destroy(it->plugin);
    }
  }
};

Plugins::Plugins()
  : impl_(std::make_unique<Impl>()) {
}

Plugins::~Plugins() = default;

void Plugins::load(const std::string &path) {
  native::Library library(path);
  {
    auto entry = reinterpret_cast<PluginEntry>(library.symbol(plugin_entry_symbol));
    if (!entry)
      throw std::runtime_error("missing plugin entry point");
    const auto *d = entry();
    if (!d || d->api_version != plugin_api_version || !d->abi_tag ||
      std::strcmp(d->abi_tag, plugin_abi_tag) != 0 || !d->id || !*d->id || !d->name || !d->create ||
      !d->destroy)
      throw std::runtime_error("incompatible or invalid plugin descriptor");
    for (const auto &p : impl_->loaded)
      if (std::strcmp(p.descriptor->id, d->id) == 0)
        throw std::runtime_error("duplicate plugin id");
    // Allocate before transferring library ownership so allocation failure cannot unload its
    // vtable.
    impl_->loaded.reserve(impl_->loaded.size() + 1);
    auto *plugin = d->create();
    if (!plugin)
      throw std::runtime_error("plugin creation failed");
    try {
      impl_->loaded.push_back({std::move(library), d, plugin});
    } catch (...) {
      d->destroy(plugin);
      throw;
    }
  }
}

void Plugins::discover(const std::string &directory) {
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) {
    if (ec)
      impl_->errors.push_back(directory + ": " + ec.message());
    return;
  }
  std::vector<std::filesystem::path> paths;
  packages::Manager managed(directory);
  if (auto enabled = managed.enabled())
    paths = std::move(*enabled);
  else
    impl_->errors.push_back(enabled.error().message);
  for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end;
    it.increment(ec))
    if (it->path().extension() == native::library_extension())
      paths.push_back(it->path());
  if (ec)
    impl_->errors.push_back(directory + ": " + ec.message());
  // Stable ordering makes duplicate-ID handling and the displayed panel order predictable.
  std::sort(paths.begin(), paths.end());
  for (const auto &p : paths)
    try {
      load(p.string());
    } catch (const std::exception &e) {
      impl_->errors.push_back(p.string() + ": " + e.what());
    }
}

std::vector<LanguagePlugin *> Plugins::languages() const {
  std::vector<LanguagePlugin *> out;
  for (auto &p : impl_->loaded)
    out.push_back(p.plugin);
  return out;
}

const std::vector<std::string> &Plugins::errors() const {
  return impl_->errors;
}
} // namespace pawingo
