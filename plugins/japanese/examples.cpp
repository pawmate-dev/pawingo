module;
// Pawingo stores UTF-8 strings; omit upstream wide-string adapters removed by C++26.
#define SQLITE_ORM_OMITS_CODECVT
#include <sqlite_orm/sqlite_orm.h>
#include <zlib.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

module pawingo.japanese.examples;

import std;
import pawingo.io;
import pawingo.japanese.settings;
import pawingo.japanese.assets;

namespace pawingo::japanese {
namespace {
using namespace sqlite_orm;

struct Failure {
  ExampleError error;
};

[[noreturn]] void fail(ExampleErrorKind kind, std::string message) {
  throw Failure{{kind, std::move(message)}};
}

void cancelled(std::stop_token stop) {
  if (stop.stop_requested())
    fail(ExampleErrorKind::cancelled, "Example operation cancelled.");
}

template <class F> auto guarded(F work) -> std::expected<decltype(work()), ExampleError> {
  try {
    return work();
  } catch (const Failure &f) {
    return std::unexpected(f.error);
  } catch (const std::filesystem::filesystem_error &e) {
    return std::unexpected(ExampleError{ExampleErrorKind::io, e.what()});
  } catch (const std::exception &e) {
    return std::unexpected(ExampleError{ExampleErrorKind::storage, e.what()});
  }
}

struct Sentence {
  std::int64_t id;
  std::string text;
};

struct Translation {
  std::int64_t id, sentence_id, translation_id;
  std::string language, text;
};

struct Link {
  std::int64_t id = 0, sentence_id = 0;
  std::string headword, reading;
  std::int64_t entry_id = 0;
  int sense = 0, checked = 0;
};

auto database(const std::filesystem::path &path) {
  return make_storage(path.string(),
    make_table("example_sentences",
      make_column("id", &Sentence::id, primary_key()),
      make_column("text", &Sentence::text)),
    make_table("example_translations",
      make_column("id", &Translation::id, primary_key()),
      make_column("sentence_id", &Translation::sentence_id),
      make_column("translation_id", &Translation::translation_id),
      make_column("language", &Translation::language),
      make_column("text", &Translation::text)),
    make_table("example_links",
      make_column("id", &Link::id, primary_key()),
      make_column("sentence_id", &Link::sentence_id),
      make_column("headword", &Link::headword),
      make_column("reading", &Link::reading),
      make_column("entry_id", &Link::entry_id),
      make_column("sense", &Link::sense),
      make_column("checked", &Link::checked)));
}

void initialize(auto &db) {
  db.on_open = [](sqlite3 *connection) {
    char *raw = nullptr;
    if (sqlite3_exec(connection, assets::japanese_examples_sql, nullptr, nullptr, &raw) !=
      SQLITE_OK) {
      const std::string message = raw ? raw : sqlite3_errmsg(connection);
      sqlite3_free(raw);
      fail(ExampleErrorKind::storage, message);
    }
  };
  db.open_forever();
}

void commit(const std::filesystem::path &from, const std::filesystem::path &to) {
#ifdef _WIN32
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING))
    fail(ExampleErrorKind::io, "Cannot replace example resource.");
#else
  std::filesystem::rename(from, to);
#endif
}

std::int64_t positive_id(std::string_view text) {
  std::int64_t value = 0;
  auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0)
    fail(ExampleErrorKind::format, "Expected a positive sentence or dictionary ID.");
  return value;
}

std::vector<std::string> split_columns(const std::string &line) {
  std::vector<std::string> result;
  std::size_t start = 0;
  for (;;) {
    auto end = line.find('\t', start);
    result.push_back(line.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos)
      return result;
    start = end + 1;
  }
}

void validate_text(const std::string &text) {
  if (text.empty() || std::ranges::any_of(text, [](unsigned char c) {
        return c < 32 || c == 127;
      }))
    fail(ExampleErrorKind::format, "Empty sentence or unexpected control character.");
}

// zlib reads both plain and gzip streams. Bounds apply to expanded bytes, not archive size.
template <class F> void lines(const std::filesystem::path &path, std::stop_token stop, F consume) {
#ifdef _WIN32
  auto raw = gzopen_w(path.c_str(), "rb");
#else
  auto raw = gzopen(path.c_str(), "rb");
#endif
  if (!raw)
    fail(ExampleErrorKind::io, "Cannot open example input file.");
  std::unique_ptr<gzFile_s, decltype(&gzclose)> file(raw, gzclose);
  const auto max_bytes = settings().integer("examples.expanded_bytes", 1, 1073741824);
  const auto max_line = settings().integer("examples.max_line_bytes", 1, 1048576);
  std::array<char, 8192> buffer;
  std::string line;
  std::size_t bytes = 0;
  bool first = true;
  auto emit = [&] {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (first && line.starts_with("\xef\xbb\xbf"))
      line.erase(0, 3);
    first = false;
    if (!line.empty() && !line.starts_with('#'))
      consume(line);
    line.clear();
  };
  for (;;) {
    cancelled(stop);
    const auto count = gzread(file.get(), buffer.data(), buffer.size());
    if (count < 0)
      fail(ExampleErrorKind::format, "Cannot decompress example corpus.");
    if (count == 0)
      break;
    bytes += count;
    if (bytes > static_cast<std::size_t>(max_bytes))
      fail(ExampleErrorKind::format, "Example input exceeds its expanded size limit.");
    for (int i = 0; i < count; ++i) {
      if (buffer[i] == '\n')
        emit();
      else {
        line += buffer[i];
        if (line.size() > static_cast<std::size_t>(max_line))
          fail(ExampleErrorKind::format, "Example line exceeds its size limit.");
      }
    }
  }
  int status = Z_OK;
  gzerror(file.get(), &status);
  if (status != Z_OK && status != Z_STREAM_END)
    fail(ExampleErrorKind::format, "Incomplete example archive.");
  if (!line.empty())
    emit();
  cancelled(stop);
}

std::optional<Link> index_term(std::string_view token, std::int64_t sentence) {
  Link link;
  link.sentence_id = sentence;
  const auto head_end = token.find_first_of("([{~|");
  link.headword = std::string(token.substr(0, head_end));
  if (link.headword.empty())
    return {};
  auto remaining = head_end == std::string_view::npos ? std::string_view{} : token.substr(head_end);
  auto field = [&](char open, char close) -> std::optional<std::string_view> {
    if (remaining.empty() || remaining.front() != open)
      return {};
    const auto end = remaining.find(close);
    if (end == std::string_view::npos) {
      remaining = "!";
      return {};
    }
    auto value = remaining.substr(1, end - 1);
    remaining.remove_prefix(end + 1);
    return value;
  };
  try {
    if (auto value = field('(', ')')) {
      if (value->starts_with('#'))
        link.entry_id = positive_id(value->substr(1));
      else
        link.reading = reading_key(std::string(*value));
    }
    if (auto value = field('[', ']')) {
      const auto sense = positive_id(*value);
      if (sense > 10000)
        return {};
      link.sense = static_cast<int>(sense);
    }
    field('{', '}'); // Surface inflections are intentionally not used as dictionary identities.
    if (remaining.starts_with('~')) {
      link.checked = 1;
      remaining.remove_prefix(1);
    }
    if (remaining.starts_with('|'))
      remaining = {}; // Legacy maintenance marker.
  } catch (const Failure &) {
    return {};
  }
  if (!remaining.empty())
    return {};
  return link;
}

void parse_indices(const std::string &text, std::int64_t id, auto consume) {
  std::size_t start = 0;
  int depth = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i < text.size()) {
      if (text[i] == '{' || text[i] == '(' || text[i] == '[')
        ++depth;
      if (text[i] == '}' || text[i] == ')' || text[i] == ']')
        --depth;
    }
    if (i == text.size() || (text[i] == ' ' && depth == 0)) {
      if (auto link = index_term(std::string_view(text).substr(start, i - start), id))
        consume(*link);
      start = i + 1;
    }
  }
}

void put_translation(auto &db, const Translation &value) {
  // sqlite_orm 1.9 serializes variadic conflict targets separately; let SQLite match the unique
  // key.
  auto statement = db.prepare(insert(into<Translation>(),
    columns(&Translation::sentence_id,
      &Translation::translation_id,
      &Translation::language,
      &Translation::text),
    values(std::make_tuple(value.sentence_id, value.translation_id, value.language, value.text)),
    on_conflict().do_update(set(c(&Translation::text) = excluded(&Translation::text)))));
  db.execute(statement);
}
} // namespace

struct Examples::Impl {
  std::filesystem::path path;
  std::mutex mutex;
};

std::filesystem::path examples_path() {
  return config::data_path(settings().text("examples.path"));
}

Examples::Examples(std::filesystem::path path)
  : impl_(std::make_unique<Impl>()) {
  impl_->path = std::move(path);
}

Examples::~Examples() = default;

const std::filesystem::path &Examples::path() const {
  return impl_->path;
}

std::expected<std::vector<Example>, ExampleError> Examples::find(const ExampleRequest &request,
  std::stop_token stop) {
  std::lock_guard lock(impl_->mutex);
  return guarded([&] {
    cancelled(stop);
    std::vector<Example> result;
    if (request.entry.sequence <= 0 || request.expression.empty())
      fail(ExampleErrorKind::invalid_input, "Example lookup requires a dictionary identity.");
    if (!std::filesystem::is_regular_file(impl_->path))
      return result;
    auto db = database(impl_->path);
    const auto target = gloss_language(request.target);
    const auto max_results = settings().integer("examples.result_limit", 1, 100);
    const auto candidates = settings().integer("examples.candidate_limit", 1, 100000);
    auto links =
      db.get_all<Link>(where(c(&Link::entry_id) == request.entry.sequence ||
                         (c(&Link::entry_id) == 0 && c(&Link::headword) == request.expression)),
        multi_order_by(order_by(&Link::checked).desc(),
          order_by(&Link::sentence_id),
          order_by(&Link::id)),
        limit(candidates));
    std::set<std::int64_t> seen;
    for (const auto &link : links) {
      cancelled(stop);
      if ((!link.reading.empty() && link.reading != reading_key(request.reading)) ||
        (link.sense != 0 && link.sense != request.entry.sense) ||
        (link.entry_id == 0 &&
          (link.reading.empty() ? !request.entry.unique_headword : !request.entry.unique_reading)))
        continue;
      if (!seen.insert(link.sentence_id).second)
        continue;
      auto sentence = db.get_pointer<Sentence>(link.sentence_id);
      if (!sentence)
        continue;
      auto translations = db.get_all<Translation>(
        where(c(&Translation::sentence_id) == sentence->id && c(&Translation::language) == target),
        order_by(&Translation::translation_id),
        limit(1)
      );
      Example value;
      value.sentence_id = sentence->id;
      value.text = sentence->text;
      value.translation_language = target;
      value.source_url = settings().text("examples.sentence_url") + std::to_string(sentence->id);
      value.attribution = settings().text("examples.attribution");
      value.license = settings().text("examples.license");
      value.checked = link.checked;
      value.sense_link = link.sense != 0;
      if (!translations.empty()) {
        value.translation_id = translations[0].translation_id;
        value.translation = translations[0].text;
        value.translation_url =
          settings().text("examples.sentence_url") + std::to_string(value.translation_id);
      }
      result.push_back(std::move(value));
      if (result.size() == static_cast<std::size_t>(max_results))
        break;
    }
    return result;
  });
}

std::expected<std::size_t, ExampleError>
Examples::install_index(const std::filesystem::path &source, std::stop_token stop) {
  std::lock_guard lock(impl_->mutex);
  return guarded([&] {
    cancelled(stop);
    io::TemporaryArtifact candidate(impl_->path.parent_path());
    if (std::filesystem::exists(impl_->path))
      std::filesystem::copy_file(impl_->path, candidate.file());
    std::size_t count = 0, links = 0;
    const auto row_limit = settings().integer("examples.max_records", 1, 10000000);
    const auto link_limit = settings().integer("examples.max_links", 1, 10000000);
    {
      auto db = database(candidate.file());
      initialize(db);
      db.transaction([&] {
        db.remove_all<Link>();
        db.remove_all<Sentence>();
        db.remove_all<Translation>(where(c(&Translation::language) == "eng"));
        lines(source, stop, [&](const std::string &line) {
          auto fields = split_columns(line);
          if (fields.size() != 5)
            fail(ExampleErrorKind::format, "Expected WWWJDIC five-column TSV.");
          const auto id = positive_id(fields[0]), translation = positive_id(fields[1]);
          validate_text(fields[2]);
          validate_text(fields[3]);
          auto previous = db.get_pointer<Sentence>(id);
          if (previous && previous->text != fields[2])
            fail(ExampleErrorKind::format, "Conflicting Japanese sentence IDs.");
          if (!previous)
            db.replace(Sentence{id, fields[2]});
          put_translation(db, {0, id, translation, "eng", fields[3]});
          parse_indices(fields[4], id, [&](const Link &link) {
            cancelled(stop);
            if (++links > static_cast<std::size_t>(link_limit))
              fail(ExampleErrorKind::format, "Too many example indices.");
            db.insert(link);
          });
          if (++count > static_cast<std::size_t>(row_limit))
            fail(ExampleErrorKind::format, "Too many example sentences.");
        });
        if (!count || !links)
          fail(ExampleErrorKind::format, "Example file has no usable indexed sentences.");
        return true;
      });
    }
    cancelled(stop);
    commit(candidate.file(), impl_->path);
    return count;
  });
}

std::expected<std::size_t, ExampleError>
Examples::install_translations(const TranslationImport &request, std::stop_token stop) {
  std::lock_guard lock(impl_->mutex);
  return guarded([&] {
    cancelled(stop);
    const auto language = gloss_language(request.language);
    if (language.size() != 3 || !std::ranges::all_of(language, [](unsigned char c) {
          return c >= 'a' && c <= 'z';
        }))
      fail(ExampleErrorKind::invalid_input,
        "Choose a three-letter target language or supported alias.");
    if (!std::filesystem::is_regular_file(impl_->path))
      fail(ExampleErrorKind::invalid_input, "Install the indexed example corpus first.");
    io::TemporaryArtifact candidate(impl_->path.parent_path());
    std::filesystem::copy_file(impl_->path, candidate.file());
    std::size_t count = 0, rows = 0;
    const auto max_rows = settings().integer("examples.max_records", 1, 10000000);
    {
      auto db = database(candidate.file());
      initialize(db);
      db.transaction([&] {
        db.remove_all<Translation>(where(c(&Translation::language) == language));
        lines(request.file, stop, [&](const std::string &line) {
          if (++rows > static_cast<std::size_t>(max_rows))
            fail(ExampleErrorKind::format, "Too many translation rows.");
          auto fields = split_columns(line);
          if (fields.size() != 4)
            fail(ExampleErrorKind::format, "Expected four-column Tatoeba sentence pairs.");
          const auto id = positive_id(fields[0]), translation = positive_id(fields[2]);
          validate_text(fields[1]);
          validate_text(fields[3]);
          auto sentence = db.get_pointer<Sentence>(id);
          if (!sentence)
            return;
          if (sentence->text != fields[1])
            fail(ExampleErrorKind::format, "Japanese sentence text does not match the indexed ID.");
          put_translation(db, {0, id, translation, language, fields[3]});
          ++count;
        });
        if (!count)
          fail(ExampleErrorKind::format, "No translation pairs match the installed sentence IDs.");
        return true;
      });
    }
    cancelled(stop);
    commit(candidate.file(), impl_->path);
    return count;
  });
}
} // namespace pawingo::japanese
