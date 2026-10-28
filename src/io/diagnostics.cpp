module;
#include <cerrno>
#include <fcntl.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

module pawingo.io;

import std;

namespace pawingo::io {
namespace {
int error_descriptor() {
#ifdef _WIN32
  return _fileno(stderr);
#else
  return STDERR_FILENO;
#endif
}

void close_descriptor(int fd) {
#ifdef _WIN32
  _close(fd);
#else
  close(fd);
#endif
}

int replace_descriptor(int from, int to) {
#ifdef _WIN32
  return _dup2(from, to);
#else
  int result;
  do {
    result = dup2(from, to);
  } while (result < 0 && errno == EINTR);
  return result;
#endif
}
} // namespace

DiagnosticLog::DiagnosticLog(const std::filesystem::path &file) {
  if (!file.parent_path().empty())
    std::filesystem::create_directories(file.parent_path());
#ifdef _WIN32
  const int log =
    _wopen(file.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
#else
  const int log = open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
#endif
  if (log < 0)
    throw std::system_error(errno, std::generic_category(), "Cannot open diagnostic log");
  std::fflush(stderr);
#ifdef _WIN32
  saved_ = _dup(error_descriptor());
#else
  saved_ = fcntl(error_descriptor(), F_DUPFD_CLOEXEC, 0);
#endif
  if (saved_ < 0 || replace_descriptor(log, error_descriptor()) < 0) {
    const int failure = errno;
    if (saved_ >= 0)
      close_descriptor(saved_);
    close_descriptor(log);
    throw std::system_error(failure, std::generic_category(), "Cannot redirect diagnostics");
  }
  close_descriptor(log);
}

DiagnosticLog::~DiagnosticLog() {
  std::fflush(stderr);
  replace_descriptor(saved_, error_descriptor());
  close_descriptor(saved_);
}
} // namespace pawingo::io
