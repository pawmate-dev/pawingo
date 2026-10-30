module;
#include <version>

export module pawingo.build;

import std;

#define PAWINGO_TEXT_IMPL(x) #x
#define PAWINGO_TEXT(x) PAWINGO_TEXT_IMPL(x)
#if defined(__clang__)
#define PAWINGO_BUILD_COMPILER "clang-" __clang_version__
#elif defined(__GNUC__)
#define PAWINGO_BUILD_COMPILER "gcc-" __VERSION__
#elif defined(_MSC_FULL_VER)
#define PAWINGO_BUILD_COMPILER "msvc-" PAWINGO_TEXT(_MSC_FULL_VER)
#else
#define PAWINGO_BUILD_COMPILER "unknown"
#endif
#if defined(_GLIBCXX_RELEASE)
#define PAWINGO_BUILD_STDLIB \
  "libstdc++-" PAWINGO_TEXT(_GLIBCXX_RELEASE) "-" PAWINGO_TEXT( \
    __GLIBCXX__ \
  ) ";cxx11-" PAWINGO_TEXT(_GLIBCXX_USE_CXX11_ABI)
#elif defined(_LIBCPP_VERSION)
#define PAWINGO_BUILD_STDLIB "libc++-" PAWINGO_TEXT(_LIBCPP_VERSION)
#elif defined(_MSVC_STL_VERSION)
#define PAWINGO_BUILD_STDLIB \
  "msvc-stl-" PAWINGO_TEXT(_MSVC_STL_VERSION) "-" PAWINGO_TEXT(_MSVC_STL_UPDATE)
#else
#define PAWINGO_BUILD_STDLIB "unknown"
#endif
#if defined(__linux__) && defined(__x86_64__)
#define PAWINGO_BUILD_PLATFORM "linux-x86_64-gnu"
#elif defined(__linux__) && defined(__aarch64__)
#define PAWINGO_BUILD_PLATFORM "linux-aarch64-gnu"
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define PAWINGO_BUILD_PLATFORM "windows-x86_64"
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
#define PAWINGO_BUILD_PLATFORM "windows-aarch64"
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define PAWINGO_BUILD_PLATFORM "macos-aarch64"
#elif defined(__APPLE__) && defined(__x86_64__)
#define PAWINGO_BUILD_PLATFORM "macos-x86_64"
#else
#define PAWINGO_BUILD_PLATFORM "unsupported"
#endif

/// Compile-time build identity shared by the host, catalog and native descriptor.
export namespace pawingo::build {
inline constexpr char version[] = PAWINGO_HOST_VERSION;
inline constexpr char release[] = PAWINGO_RELEASE_BUILD;
inline constexpr char platform[] = PAWINGO_BUILD_PLATFORM;
inline constexpr char abi[] = PAWINGO_BUILD_COMPILER
  ";" PAWINGO_BUILD_STDLIB ";ftxui-6.1.9;api-1;modules-4;host-" PAWINGO_HOST_VERSION
  ";build-" PAWINGO_RELEASE_BUILD ";mode-" PAWINGO_BUILD_MODE ";" PAWINGO_BUILD_PLATFORM;
} // namespace pawingo::build
