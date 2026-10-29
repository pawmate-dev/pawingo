CREATE TABLE IF NOT EXISTS review_cards (
  vocabulary_id INTEGER PRIMARY KEY REFERENCES vocabulary(id) ON DELETE CASCADE,
  stability REAL,
  difficulty REAL,
  last_review_at INTEGER,
  due_at INTEGER NOT NULL DEFAULT 0 CHECK(due_at >= 0),
  revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0),
  CHECK ((stability IS NULL AND difficulty IS NULL AND last_review_at IS NULL)
    OR (stability > 0 AND difficulty BETWEEN 1 AND 10 AND last_review_at >= 0
      AND stability IS NOT NULL AND difficulty IS NOT NULL AND last_review_at IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS review_card_due ON review_cards(due_at, vocabulary_id);

CREATE TABLE IF NOT EXISTS fsrs_reviews (
  review_id INTEGER PRIMARY KEY REFERENCES reviews(id) ON DELETE CASCADE,
  vocabulary_id INTEGER NOT NULL REFERENCES vocabulary(id) ON DELETE CASCADE,
  revision INTEGER NOT NULL CHECK(revision > 0),
  rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 4),
  answer TEXT NOT NULL,
  elapsed_days INTEGER NOT NULL CHECK(elapsed_days >= 0),
  stability REAL NOT NULL CHECK(stability > 0),
  difficulty REAL NOT NULL CHECK(difficulty BETWEEN 1 AND 10),
  desired_retention REAL NOT NULL CHECK(desired_retention > 0 AND desired_retention < 1),
  algorithm TEXT NOT NULL DEFAULT 'fsrs-6',
  UNIQUE(vocabulary_id, revision)
);

-- Enroll legacy favorites without inventing FSRS ratings or learning activity.
INSERT OR IGNORE INTO review_cards(vocabulary_id) SELECT id FROM vocabulary;
