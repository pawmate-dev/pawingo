module;
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern "C" char **environ;
#endif
#endif

module pawingo.native;
import std;

namespace pawingo::native {
struct Library::Impl {
#ifdef _WIN32
  HMODULE handle;

  ~Impl() {
    if (handle)
      FreeLibrary(handle);
  }
#else
  void *handle;

  ~Impl() {
    if (handle)
      dlclose(handle);
  }
#endif
};

Library::Library(const std::filesystem::path &path)
  : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
  // Search the library's own directory for bundled dependencies without changing process PATH.
  impl_->handle = LoadLibraryExW(std::filesystem::absolute(path).c_str(),
    nullptr,
    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!impl_->handle)
    throw std::runtime_error("Cannot load native library: " + std::to_string(GetLastError()));
#else
  impl_->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!impl_->handle)
    throw std::runtime_error(dlerror());
#endif
}

Library::~Library() = default;
Library::Library(Library &&) noexcept = default;
Library &Library::operator=(Library &&) noexcept = default;

void *Library::symbol(const char *name) const {
#ifdef _WIN32
  auto value = reinterpret_cast<void *>(GetProcAddress(impl_->handle, name));
#else
  auto value = dlsym(impl_->handle, name);
#endif
  if (!value)
    throw std::runtime_error("Missing native symbol: " + std::string(name));
  return value;
}

std::string_view library_extension() {
#ifdef _WIN32
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

#ifdef _WIN32
namespace {
struct Handle {
  HANDLE value = nullptr;

  ~Handle() {
    if (value && value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};

std::wstring wide(const std::string &text) {
  auto length = MultiByteToWideChar(CP_UTF8,
    MB_ERR_INVALID_CHARS,
    text.data(),
    static_cast<int>(text.size()),
    nullptr,
    0);
  if (!length)
    throw std::runtime_error("Invalid UTF-8 process argument.");
  std::wstring result(length, L'\0');
  MultiByteToWideChar(CP_UTF8,
    MB_ERR_INVALID_CHARS,
    text.data(),
    static_cast<int>(text.size()),
    result.data(),
    length);
  return result;
}

// Match the Windows CRT argument parser, including trailing backslashes before a quote.
std::wstring quote(const std::string &argument) {
  auto input = argument.empty() ? std::wstring{} : wide(argument);
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (auto ch : input) {
    if (ch == L'\\') {
      ++slashes;
      continue;
    }
    result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (ch == L'"')
      result += L'\\';
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}
} // namespace
#endif

std::expected<int, ProcessError> run_process(const ProcessRequest &request, std::stop_token stop) {
  if (stop.stop_requested())
    return std::unexpected(ProcessError{ProcessErrorKind::cancelled, "Process cancelled."});
  if (request.arguments.empty() || request.arguments.front().empty())
    return std::unexpected(ProcessError{ProcessErrorKind::launch, "Missing process command."});
  auto interval = std::clamp(request.poll_interval,
    std::chrono::milliseconds(1),
    std::chrono::milliseconds(1000));
#ifdef _WIN32
  Handle job{CreateJobObjectW(nullptr, nullptr)};
  if (!job.value)
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job.value,
        JobObjectExtendedLimitInformation,
        &limits,
        sizeof(limits)))
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle null{CreateFileW(L"NUL",
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    &security,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr)};
  if (null.value == INVALID_HANDLE_VALUE)
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = startup.hStdOutput = startup.hStdError = null.value;
  std::wstring command;
  for (const auto &arg : request.arguments) {
    if (!command.empty())
      command += L' ';
    command += quote(arg);
  }
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr,
        command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process))
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  Handle child{process.hProcess}, thread{process.hThread};
  if (!AssignProcessToJobObject(job.value, child.value) ||
    ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
    TerminateProcess(child.value, 1);
    WaitForSingleObject(child.value, INFINITE);
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  }
  for (;;) {
    auto status = WaitForSingleObject(child.value, static_cast<DWORD>(interval.count()));
    if (status == WAIT_OBJECT_0) {
      DWORD code = 1;
      if (GetExitCodeProcess(child.value, &code))
        return static_cast<int>(code);
      return std::unexpected(ProcessError{ProcessErrorKind::wait,
        "Cannot read process exit status."});
    }
    if (status == WAIT_FAILED || stop.stop_requested()) {
      TerminateJobObject(job.value, 1);
      WaitForSingleObject(child.value, INFINITE);
      return std::unexpected(ProcessError{stop.stop_requested() ? ProcessErrorKind::cancelled
                                                                : ProcessErrorKind::wait,
        "Process interrupted."});
    }
  }
#else
  std::vector<char *> argv;
  for (const auto &arg : request.arguments)
    argv.push_back(const_cast<char *>(arg.c_str()));
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0)
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  posix_spawnattr_t attributes;
  if (posix_spawnattr_init(&attributes) != 0) {
    posix_spawn_file_actions_destroy(&actions);
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  }
  auto result = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  if (!result)
    result = posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  if (!result)
    result = posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
  if (!result)
    result = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  if (!result)
    result = posix_spawnattr_setpgroup(&attributes, 0);
  pid_t pid = -1;
#ifdef __APPLE__
  auto environment = *_NSGetEnviron();
#else
  auto environment = environ;
#endif
  if (!result)
    result = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environment);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  if (result)
    return std::unexpected(ProcessError{ProcessErrorKind::launch,
      "Native process operation failed."});
  int status = 0;
  for (;;) {
    auto waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid)
      return WIFEXITED(status)
        ? std::expected<int, ProcessError>(WEXITSTATUS(status))
        : std::unexpected(ProcessError{ProcessErrorKind::wait, "Process exited abnormally."});
    if (waited < 0 && errno != EINTR)
      return std::unexpected(ProcessError{ProcessErrorKind::launch,
        "Native process operation failed."});
    if (stop.stop_requested()) {
      kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      return std::unexpected(ProcessError{ProcessErrorKind::cancelled, "Process cancelled."});
    }
    std::this_thread::sleep_for(interval);
  }
#endif
}
} // namespace pawingo::native
