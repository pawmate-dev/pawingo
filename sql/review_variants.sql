BEGIN IMMEDIATE;
CREATE TABLE review_cards_next (
  vocabulary_id INTEGER NOT NULL REFERENCES vocabulary(id) ON DELETE CASCADE,
  kind INTEGER NOT NULL DEFAULT 0 CHECK(kind BETWEEN 0 AND 2),
  stability REAL,
  difficulty REAL,
  last_review_at INTEGER,
  due_at INTEGER NOT NULL DEFAULT 0 CHECK(due_at >= 0),
  revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0),
  PRIMARY KEY(vocabulary_id, kind),
  CHECK ((stability IS NULL AND difficulty IS NULL AND last_review_at IS NULL)
    OR (stability > 0 AND difficulty BETWEEN 1 AND 10 AND last_review_at >= 0
      AND stability IS NOT NULL AND difficulty IS NOT NULL AND last_review_at IS NOT NULL))
);
INSERT INTO review_cards_next SELECT vocabulary_id, 0, stability, difficulty, last_review_at,
  due_at, revision FROM review_cards;
DROP TABLE review_cards;
ALTER TABLE review_cards_next RENAME TO review_cards;
CREATE INDEX review_card_due ON review_cards(due_at, vocabulary_id, kind);

CREATE TABLE fsrs_reviews_next (
  review_id INTEGER PRIMARY KEY REFERENCES reviews(id) ON DELETE CASCADE,
  vocabulary_id INTEGER NOT NULL REFERENCES vocabulary(id) ON DELETE CASCADE,
  kind INTEGER NOT NULL DEFAULT 0 CHECK(kind BETWEEN 0 AND 2),
  revision INTEGER NOT NULL CHECK(revision > 0),
  rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 4),
  answer TEXT NOT NULL,
  elapsed_days INTEGER NOT NULL CHECK(elapsed_days >= 0),
  stability REAL NOT NULL CHECK(stability > 0),
  difficulty REAL NOT NULL CHECK(difficulty BETWEEN 1 AND 10),
  desired_retention REAL NOT NULL CHECK(desired_retention > 0 AND desired_retention < 1),
  algorithm TEXT NOT NULL DEFAULT 'fsrs-6',
  UNIQUE(vocabulary_id, kind, revision)
);
INSERT INTO fsrs_reviews_next SELECT review_id, vocabulary_id, 0, revision, rating, answer,
  elapsed_days, stability, difficulty, desired_retention, algorithm FROM fsrs_reviews;
DROP TABLE fsrs_reviews;
ALTER TABLE fsrs_reviews_next RENAME TO fsrs_reviews;
COMMIT;
