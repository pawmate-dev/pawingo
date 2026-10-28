import std;
import pawingo.config;

using pawingo::config::Settings;

void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

int main() {
  try {
    Settings valid("schema: 1\nlimits: {count: 5}\nlabels: [a, b]\n", "fixture");
    require(valid.integer("limits.count", 1, 10) == 5, "read bounded integer");
    require(valid.strings("labels").size() == 2, "read list");
    bool rejected = false;
    try {
      valid.integer("limits.count", 1, 3);
    } catch (const std::runtime_error &e) {
      rejected = std::string(e.what()).find("limits.count") != std::string::npos;
    }
    require(rejected, "reject invalid range with field context");
    rejected = false;
    try {
      valid.text("missing");
    } catch (const std::runtime_error &e) {
      rejected = std::string(e.what()).find("fixture") != std::string::npos;
    }
    require(rejected, "reject missing value with document context");
    rejected = false;
    try {
      Settings wrong("schema: 2", "future");
    } catch (...) {
      rejected = true;
    }
    require(rejected, "reject unsupported schema");
    require(pawingo::config::application().integer("activity.weeks", 1, 52) == 14,
      "embedded default preserved");
    require(pawingo::config::ui_text("get_plugins") == "Get plugins",
      "embedded presentation catalog");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
