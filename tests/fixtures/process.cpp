import std;

#ifdef _WIN32
// CreateProcessW supplies UTF-16; narrow main would convert through the machine ANSI code page.
int wmain(int argc, wchar_t **argv) {
  using View = std::wstring_view;
  constexpr std::array<View, 6> expected{L"--arguments",
    L"",
    L"猫",
    L"a\"b",
    L"tail\\",
    L"space value"};
#else
int main(int argc, char **argv) {
  using View = std::string_view;
  constexpr std::array<View, 6> expected{"--arguments", "", "猫", "a\"b", "tail\\", "space value"};
#endif
  if (argc > 1 && View(argv[1]) == expected[0]) {
    if (argc != static_cast<int>(expected.size()) + 1)
      return 10;
    for (std::size_t index = 0; index < expected.size(); ++index)
      if (View(argv[index + 1]) != expected[index])
        return static_cast<int>(20 + index);
    return 0;
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));
  return 0;
}
