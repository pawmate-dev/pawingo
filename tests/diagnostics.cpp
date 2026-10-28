#include <stdio.h>

import std;
import pawingo.io;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

int main() {
  try {
    pawingo::io::TemporaryArtifact scratch(std::filesystem::temp_directory_path());
    const auto root = scratch.file().parent_path();
    const auto outer_path = root / "outer.log";
    const auto inner_path = root / "logs" / "inner.log";
    {
      pawingo::io::DiagnosticLog outer(outer_path);
      {
        pawingo::io::DiagnosticLog inner(inner_path);
        std::jthread native_worker([] {
          std::fputs("native worker diagnostic\n", stderr);
        });
      }
      std::fputs("restored stderr\n", stderr);
      try {
        pawingo::io::DiagnosticLog inner(inner_path);
        std::cerr << "C++ diagnostic\n";
        throw std::runtime_error("unwind");
      } catch (const std::runtime_error &) {
        std::fputs("restored after exception\n", stderr);
      }
      bool failed = false;
      try {
        pawingo::io::DiagnosticLog invalid(root);
      } catch (const std::system_error &) {
        failed = true;
      }
      require(failed, "opening a directory as the log reports a failure");
      std::fputs("preserved after failed redirection\n", stderr);
    }
    const auto inner = read(inner_path), outer = read(outer_path);
    require(inner.contains("native worker diagnostic") && inner.contains("C++ diagnostic"),
      "native thread and C++ diagnostics are captured and appended");
    require(!outer.contains("native worker diagnostic") && !outer.contains("C++ diagnostic"),
      "captured diagnostics do not leak to the previous destination");
    require(outer.contains("restored stderr") && outer.contains("restored after exception") &&
        outer.contains("preserved after failed redirection"),
      "stderr is restored on normal exit and unwinding, and preserved on setup failure");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
