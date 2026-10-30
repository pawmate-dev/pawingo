import std;
import pawingo.native;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main(int argc, char **argv) {
  using namespace pawingo::native;
  try {
    require(argc == 2, "process fixture required");
    auto result = run_process(
      {.arguments = {argv[1], "--arguments", "", "猫", "a\"b", "tail\\", "space value"}}
    );
    if (!result)
      throw std::runtime_error("Process argument fixture could not run: " + result.error().message);
    if (*result != 0)
      throw std::runtime_error("Native process argument mismatch; fixture exit code " +
        std::to_string(*result));
    result = run_process({.arguments = {"pawingo-command-that-does-not-exist"}});
    require(!result && result.error().kind == ProcessErrorKind::launch,
      "missing command has a typed launch failure");
    std::stop_source stopped;
    stopped.request_stop();
    result = run_process({.arguments = {argv[1]}}, stopped.get_token());
    require(!result && result.error().kind == ProcessErrorKind::cancelled,
      "pre-cancelled process is never launched");
    const auto start = std::chrono::steady_clock::now();
    std::jthread worker([&](std::stop_token stop) {
      result = run_process({.arguments = {argv[1]}}, stop);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    worker.request_stop();
    worker.join();
    require(!result && result.error().kind == ProcessErrorKind::cancelled &&
        std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
      "cancellation terminates and joins the native child promptly");
  } catch (const std::exception &error) {
    std::cout << error.what() << std::endl;
    return 1;
  }
}
