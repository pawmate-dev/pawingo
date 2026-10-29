module;
#include <expat.h>
#include <zlib.h>

module pawingo.japanese.dictionary;

import std;
import pawingo.io;
import pawingo.japanese.settings;

namespace pawingo::japanese {
namespace {
void cancelled(std::stop_token stop) {
  if (stop.stop_requested())
    throw std::runtime_error("Cancelled.");
}

std::string trim(std::string text) {
  const auto start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return {};
  return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}

// Fold ASCII and ordinary katakana, preserving kanji and other UTF-8 bytes.
std::string normalized(const std::string &text) {
  std::string result;
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (i + 2 < text.size() && c == 0xe3 &&
      (static_cast<unsigned char>(text[i + 1]) & 0xc0) == 0x80 &&
      (static_cast<unsigned char>(text[i + 2]) & 0xc0) == 0x80) {
      unsigned cp = ((c & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
      if (cp >= 0x30a1 && cp <= 0x30f6) {
        cp -= 0x60;
        result += static_cast<char>(0xe0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (cp & 0x3f));
        i += 3;
        continue;
      }
    }
    result += c < 128 ? static_cast<char>(std::tolower(c)) : text[i];
    ++i;
  }
  return trim(std::move(result));
}

struct Reading {
  std::string text;
  std::vector<std::string> restrictions;
  bool kana_only = false;
};

struct Sense {
  std::vector<std::string> spellings, readings;
  std::map<std::string, std::vector<std::string>> glosses;
};

struct Entry {
  std::string sequence;
  std::vector<std::string> spellings;
  std::vector<Reading> readings;
  std::vector<Sense> senses;
};

struct Row {
  Vocabulary word;
  std::string expression, reading, meaning;
  std::int64_t sequence = 0;
  std::uint32_t sense = 0;
};

using Rows = std::vector<Row>;

bool allows(const std::vector<std::string> &values, const std::string &value) {
  return values.empty() || std::find(values.begin(), values.end(), value) != values.end();
}

struct Parser {
  XML_Parser xml = XML_ParserCreate(nullptr);
  const std::string vocabulary_language = settings().text("plugin.language");
  const std::string source_prefix = settings().text("dictionary.source_prefix") + " ";
  Entry entry;
  Reading reading;
  Sense sense;
  std::string text, language;
  Rows rows;
  std::stop_token stop;
  std::exception_ptr failure;
  bool root = false;

  explicit Parser(std::stop_token token)
    : stop(token) {
    if (!xml)
      throw std::runtime_error("Cannot create XML parser.");
    XML_SetUserData(xml, this);
    XML_SetElementHandler(xml, begin, end);
    XML_SetCharacterDataHandler(xml, characters);
    XML_SetParamEntityParsing(xml, XML_PARAM_ENTITY_PARSING_NEVER);
    XML_SetExternalEntityRefHandler(xml,
      [](XML_Parser, const XML_Char *, const XML_Char *, const XML_Char *, const XML_Char *)
        -> int {
        return XML_STATUS_ERROR;
      });
  }

  ~Parser() {
    XML_ParserFree(xml);
  }

  template <class F> void guarded(F f) noexcept {
    try {
      cancelled(stop);
      f();
    } catch (...) {
      failure = std::current_exception();
      XML_StopParser(xml, XML_FALSE);
    }
  }

  static void XMLCALL begin(void *user, const char *tag, const char **attributes) noexcept {
    auto &p = *static_cast<Parser *>(user);
    p.guarded([&] {
      std::string name(tag);
      p.text.clear();
      if (!p.root) {
        if (name != "JMdict")
          throw std::runtime_error("Expected a JMdict XML dictionary.");
        p.root = true;
      }
      if (name == "entry")
        p.entry = {};
      if (name == "r_ele")
        p.reading = {};
      if (name == "sense")
        p.sense = {};
      if (name == "re_nokanji")
        p.reading.kana_only = true;
      if (name == "gloss") {
        p.language = "eng";
        for (int i = 0; attributes[i]; i += 2)
          if (std::string(attributes[i]) == "xml:lang")
            p.language = attributes[i + 1];
      }
    });
  }

  static void XMLCALL characters(void *user, const char *text, int length) noexcept {
    auto &p = *static_cast<Parser *>(user);
    p.guarded([&] {
      static const auto field_limit =
        static_cast<std::size_t>(settings().integer("dictionary.max_field_bytes", 1, 16777216));
      if (p.text.size() + length > field_limit)
        throw std::runtime_error("Dictionary field is too large.");
      p.text.append(text, length);
    });
  }

  static void XMLCALL end(void *user, const char *tag) noexcept {
    auto &p = *static_cast<Parser *>(user);
    p.guarded([&] {
      std::string name(tag);
      const auto value = trim(p.text);
      if (name == "ent_seq")
        p.entry.sequence = value;
      else if (name == "keb")
        p.entry.spellings.push_back(value);
      else if (name == "reb")
        p.reading.text = value;
      else if (name == "re_restr")
        p.reading.restrictions.push_back(value);
      else if (name == "r_ele")
        p.entry.readings.push_back(std::move(p.reading));
      else if (name == "stagk")
        p.sense.spellings.push_back(value);
      else if (name == "stagr")
        p.sense.readings.push_back(value);
      else if (name == "gloss" && !value.empty())
        p.sense.glosses[p.language].push_back(value);
      else if (name == "sense")
        p.entry.senses.push_back(std::move(p.sense));
      else if (name == "entry")
        p.emit();
      p.text.clear();
    });
  }

  void emit() {
    std::int64_t sequence = 0;
    const auto parsed = std::from_chars(entry.sequence.data(),
      entry.sequence.data() + entry.sequence.size(),
      sequence);
    if (parsed.ec != std::errc{} || parsed.ptr != entry.sequence.data() + entry.sequence.size())
      sequence = 0;
    for (const auto &r : entry.readings) {
      if (r.text.empty())
        continue;
      const auto spellings =
        entry.spellings.empty() || r.kana_only ? std::vector<std::string>{r.text} : entry.spellings;
      for (const auto &spelling : spellings) {
        if (!allows(r.restrictions, spelling))
          continue;
        std::uint32_t sense_number = 0;
        for (const auto &s : entry.senses) {
          ++sense_number;
          if (!allows(s.spellings, spelling) || !allows(s.readings, r.text))
            continue;
          for (const auto &[language, glosses] : s.glosses) {
            std::string meaning;
            for (const auto &gloss : glosses) {
              if (!meaning.empty())
                meaning += "; ";
              meaning += gloss;
            }
            Vocabulary v{vocabulary_language,
              spelling,
              r.text,
              language,
              meaning,
              source_prefix + entry.sequence,
              ""};
            rows.push_back({std::move(v),
              normalized(spelling),
              normalized(r.text),
              normalized(meaning),
              sequence,
              sense_number});
            static const auto row_limit =
              static_cast<std::size_t>(settings().integer("dictionary.max_records", 1, 10000000));
            if (rows.size() > row_limit)
              throw std::runtime_error("Dictionary is too large.");
          }
        }
      }
    }
  }
};

std::shared_ptr<const Rows> parse(const std::filesystem::path &path, std::stop_token stop) {
  gzFile input = gzopen(path.c_str(), "rb");
  if (!input)
    throw std::runtime_error("Cannot open dictionary: " + path.string());
  try {
    Parser parser(stop);
    std::array<char, 65536> buffer;
    std::size_t uncompressed = 0;
    for (;;) {
      cancelled(stop);
      const int count = gzread(input, buffer.data(), static_cast<unsigned>(buffer.size()));
      if (count < 0)
        throw std::runtime_error("Cannot decompress dictionary.");
      uncompressed += static_cast<std::size_t>(count);
      if (uncompressed >
        static_cast<std::size_t>(settings().integer("dictionary.expanded_bytes", 1, 1073741824)))
        throw std::runtime_error("Dictionary exceeds the import size limit.");
      int gzip_error = Z_OK;
      gzerror(input, &gzip_error);
      if (gzip_error != Z_OK && gzip_error != Z_STREAM_END)
        throw std::runtime_error("Incomplete or damaged dictionary archive.");
      if (XML_Parse(parser.xml, buffer.data(), count, count == 0) == XML_STATUS_ERROR) {
        if (parser.failure)
          std::rethrow_exception(parser.failure);
        throw std::runtime_error(std::string("Invalid JMdict XML: ") +
          XML_ErrorString(XML_GetErrorCode(parser.xml)));
      }
      if (count == 0)
        break;
    }
    if (!parser.root || parser.rows.empty())
      throw std::runtime_error("Dictionary has no usable JMdict entries.");
    const auto closed = gzclose(input);
    input = nullptr;
    if (closed != Z_OK)
      throw std::runtime_error("Cannot finish reading dictionary archive.");
    return std::make_shared<const Rows>(std::move(parser.rows));
  } catch (...) {
    if (input)
      gzclose(input);
    throw;
  }
}
} // namespace

std::filesystem::path dictionary_path() {
  return config::data_path(settings().text("dictionary.path"));
}

std::vector<std::filesystem::path> dictionary_paths() {
  std::vector<std::filesystem::path> result;
  if (const char *path = std::getenv(settings().text("dictionary.override_env").c_str());
    path && *path)
    result.emplace_back(path);
  result.push_back(dictionary_path());
  std::vector<std::filesystem::path> unique;
  for (const auto &path : result)
    if (std::find(unique.begin(), unique.end(), path) == unique.end() &&
      std::filesystem::is_regular_file(path))
      unique.push_back(path);
  return unique;
}

std::string gloss_language(const std::string &language) {
  auto normalized = ::pawingo::japanese::normalized(language);
  if (const auto separator = normalized.find('-'); separator != std::string::npos)
    normalized.resize(separator);
  static const auto codes = settings().mapping("dictionary.gloss_languages");
  auto it = codes.find(normalized);
  return it == codes.end() ? normalized : it->second;
}

std::string reading_key(const std::string &reading) {
  return normalized(reading);
}

struct Dictionary::Impl {
  struct Cached {
    std::filesystem::file_time_type modified;
    std::uintmax_t size;
    std::shared_ptr<const Rows> rows;
  };

  std::mutex mutex;
  std::map<std::filesystem::path, Cached> cache;

  std::shared_ptr<const Rows> load(const std::filesystem::path &path, std::stop_token stop) {
    auto modified = std::filesystem::last_write_time(path);
    auto size = std::filesystem::file_size(path);
    {
      std::lock_guard lock(mutex);
      auto it = cache.find(path);
      if (it != cache.end() && it->second.modified == modified && it->second.size == size)
        return it->second.rows;
    }
    auto rows = parse(path, stop);
    {
      std::lock_guard lock(mutex);
      cache.insert_or_assign(path, Cached{modified, size, rows});
    }
    return rows;
  }
};

Dictionary::Dictionary()
  : impl_(std::make_unique<Impl>()) {
}

Dictionary::~Dictionary() = default;

std::vector<Vocabulary> Dictionary::search(const std::string &query,
  const std::string &target,
  const std::vector<std::filesystem::path> &paths,
  std::stop_token stop) {
  const auto needle = normalized(query);
  const auto language = gloss_language(target);
  if (needle.empty() || language.empty())
    return {};
  const auto limit =
    static_cast<std::size_t>(settings().integer("dictionary.result_limit", 1, 1000));
  std::array<std::vector<Vocabulary>, 6> buckets;
  std::set<std::string> seen;
  for (const auto &path : paths) {
    const auto rows = impl_->load(path, stop);
    for (const auto &row : *rows) {
      cancelled(stop);
      if (row.word.meaning_language != language)
        continue;
      int rank = row.expression == needle || row.reading == needle              ? 0
        : row.expression.starts_with(needle) || row.reading.starts_with(needle) ? 1
        : row.meaning == needle                                                 ? 2
        : row.meaning.starts_with(needle)                                       ? 3
        : row.expression.find(needle) != std::string::npos ||
          row.reading.find(needle) != std::string::npos
        ? 4
        : row.meaning.find(needle) != std::string::npos ? 5
                                                        : -1;
      if (rank < 0 || buckets[rank].size() >= limit)
        continue;
      const auto key = row.word.expression + '\n' + row.word.reading + '\n' + row.word.meaning +
        '\n' + row.word.source;
      if (seen.insert(key).second)
        buckets[rank].push_back(row.word);
    }
  }
  std::vector<Vocabulary> result;
  for (auto &bucket : buckets)
    for (auto &word : bucket) {
      if (result.size() == limit)
        return result;
      result.push_back(std::move(word));
    }
  return result;
}

std::size_t Dictionary::install(const std::filesystem::path &source, std::stop_token stop) {
  cancelled(stop);
  const auto target = dictionary_path();
  io::TemporaryArtifact candidate(target.parent_path());
  std::filesystem::copy_file(source, candidate.file());
  cancelled(stop);
  auto rows = parse(candidate.file(), stop);
  cancelled(stop);
  std::filesystem::rename(candidate.file(), target);
  std::lock_guard lock(impl_->mutex);
  impl_->cache.insert_or_assign(
    target,
    Impl::Cached{std::filesystem::last_write_time(target), std::filesystem::file_size(target), rows}
  );
  return rows->size();
}

std::optional<EntryReference> Dictionary::reference(const Vocabulary &word,
  const std::vector<std::filesystem::path> &paths,
  std::stop_token stop) {
  std::optional<EntryReference> result;
  std::set<std::pair<std::int64_t, std::string>> identities;
  for (const auto &path : paths) {
    const auto rows = impl_->load(path, stop);
    for (const auto &row : *rows) {
      cancelled(stop);
      if (row.word.expression != word.expression)
        continue;
      identities.emplace(row.sequence, row.word.reading);
      if (row.word.source == word.source && row.word.reading == word.reading &&
        row.word.meaning_language == word.meaning_language && row.word.meaning == word.meaning &&
        row.sequence > 0) {
        if (!result)
          result = EntryReference{row.sequence, row.sense, false};
        else if (result->sequence != row.sequence || result->sense != row.sense)
          result->sense = 0;
      }
    }
  }
  if (result) {
    result->unique_headword = identities.size() == 1;
    result->unique_reading = std::ranges::count_if(identities, [&](const auto &identity) {
      return identity.second == word.reading;
    }) == 1;
  }
  return result;
}
} // namespace pawingo::japanese
