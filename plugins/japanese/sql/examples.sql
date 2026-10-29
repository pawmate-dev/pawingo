CREATE TABLE IF NOT EXISTS example_sentences (
  id INTEGER PRIMARY KEY,
  text TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS example_translations (
  id INTEGER PRIMARY KEY,
  sentence_id INTEGER NOT NULL,
  translation_id INTEGER NOT NULL,
  language TEXT NOT NULL,
  text TEXT NOT NULL,
  UNIQUE(sentence_id, translation_id, language)
);
CREATE TABLE IF NOT EXISTS example_links (
  id INTEGER PRIMARY KEY,
  sentence_id INTEGER NOT NULL,
  headword TEXT NOT NULL,
  reading TEXT NOT NULL,
  entry_id INTEGER NOT NULL,
  sense INTEGER NOT NULL,
  checked INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS example_link_entry ON example_links(entry_id, checked DESC);
CREATE INDEX IF NOT EXISTS example_link_headword ON example_links(headword, checked DESC);
CREATE INDEX IF NOT EXISTS example_translation_lookup ON example_translations(sentence_id, language);
