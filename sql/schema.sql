PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS vocabulary (
  id INTEGER PRIMARY KEY,
  language TEXT NOT NULL,
  expression TEXT NOT NULL,
  reading TEXT NOT NULL,
  meaning_language TEXT NOT NULL,
  meaning TEXT NOT NULL,
  source TEXT NOT NULL,
  context TEXT NOT NULL,
  UNIQUE (language, expression, reading, meaning_language, meaning, source, context)
);

CREATE TABLE IF NOT EXISTS resources (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  kind INTEGER NOT NULL,
  language TEXT NOT NULL,
  location TEXT NOT NULL,
  enabled INTEGER NOT NULL,
  priority INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS online_policy (
  service TEXT PRIMARY KEY,
  policy INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS reviews (
  id INTEGER PRIMARY KEY,
  vocabulary_id INTEGER NOT NULL REFERENCES vocabulary(id) ON DELETE CASCADE,
  result INTEGER NOT NULL,
  reviewed_at INTEGER NOT NULL,
  next_review_at INTEGER NOT NULL
);
