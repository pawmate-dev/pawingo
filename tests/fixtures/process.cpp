import std;

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--arguments") {
    return argc == 7 && std::string_view(argv[2]).empty() && std::string_view(argv[3]) == "猫" &&
        std::string_view(argv[4]) == "a\"b" && std::string_view(argv[5]) == "tail\\" &&
        std::string_view(argv[6]) == "space value"
      ? 0
      : 2;
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));
  return 0;
}
