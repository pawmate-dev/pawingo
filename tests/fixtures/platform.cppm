module;
#include <stdlib.h>
export module pawingo.tests.platform;
import std;

export namespace pawingo::tests {
/// Sets a test-local environment value using the platform CRT; callers serialize mutations.
inline void environment(const char *name, const std::string &value) {
#ifdef _WIN32
  if (_putenv_s(name, value.c_str()))
    throw std::runtime_error("Cannot set test environment.");
#else
  if (setenv(name, value.c_str(), 1))
    throw std::runtime_error("Cannot set test environment.");
#endif
}

/// Removes a test-local environment override.
inline void unset_environment(const char *name) {
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

inline constexpr char path_separator =
#ifdef _WIN32
  ';';
#else
  ':';
#endif
} // namespace pawingo::tests
