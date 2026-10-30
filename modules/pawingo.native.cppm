/// @file
/// Native library ownership and cancellable child processes behind platform adapters.
export module pawingo.native;

import std;

export namespace pawingo::native {
/// Owns a trusted native library; all borrowed symbols expire when this object is destroyed.
class Library {
public:
  /// Loads a library or throws std::runtime_error with the operating-system failure.
  explicit Library(const std::filesystem::path &path);
  ~Library();
  Library(Library &&) noexcept;
  Library &operator=(Library &&) noexcept;
  Library(const Library &) = delete;
  Library &operator=(const Library &) = delete;
  /// Looks up a required C entry point or throws std::runtime_error.
  void *symbol(const char *name) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Native plugin filename extension for the current operating system.
std::string_view library_extension();

/// Child arguments are passed directly without invoking a shell.
struct ProcessRequest {
  std::vector<std::string> arguments;
  std::chrono::milliseconds poll_interval{10};
};

/// Native process failure categories are independent of platform error messages.
enum class ProcessErrorKind { launch, wait, cancelled };

/// Recoverable native process failure.
struct ProcessError {
  ProcessErrorKind kind;
  std::string message;
};

/// Runs with discarded standard streams; returns the exit status or a typed failure.
/// Cancellation terminates the owned process group/job and waits before returning.
std::expected<int, ProcessError> run_process(const ProcessRequest &request,
  std::stop_token stop = {});
} // namespace pawingo::native
