// Fail early when a runner cannot compile, import and link the required standard-library APIs.
import std;

int main() {
  std::expected<int, std::string> result = 1;
  std::stop_source source;
  std::stop_callback callback(source.get_token(), [&] {
    result = 0;
  });
  std::jthread worker([] {
  });
  worker.join();
  source.request_stop();
  return result.value();
}
