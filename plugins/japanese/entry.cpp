#if defined(_WIN32)
#define PAWINGO_NATIVE_EXPORT __declspec(dllexport)
#else
#define PAWINGO_NATIVE_EXPORT __attribute__((visibility("default")))
#endif

import pawingo.japanese.plugin;

extern "C" PAWINGO_NATIVE_EXPORT const pawingo::PluginDescriptor *pawingo_plugin_v1() {
  return pawingo::japanese::descriptor();
}
