#ifndef PAWINGO_FSRS_H
#define PAWINGO_FSRS_H

/* Generated from native/fsrs/src/lib.rs. Do not edit. */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Success.
 */
#define PAWINGO_FSRS_OK 0

/**
 * Invalid pointer, reading history, memory state, elapsed time or retention.
 */
#define PAWINGO_FSRS_INVALID_INPUT 1

/**
 * Unsupported or non-finite model parameters.
 */
#define PAWINGO_FSRS_INVALID_PARAMETERS 2

/**
 * The model produced an invalid result.
 */
#define PAWINGO_FSRS_BACKEND_ERROR 3

/**
 * An unexpected unwinding panic was contained at the boundary.
 */
#define PAWINGO_FSRS_PANIC 4

/**
 * Opaque immutable model, exclusively owned by its C++ RAII wrapper.
 */
typedef struct PawingoFsrs PawingoFsrs;

/**
 * Model state; stability is measured in days and difficulty ranges from 1 to 10.
 */
typedef struct PawingoFsrsMemory {
  /**
   * Stability in days; must be positive and finite.
   */
  float stability;
  /**
   * Difficulty in the inclusive range [1, 10].
   */
  float difficulty;
} PawingoFsrsMemory;

/**
 * Result for one of the four ratings.
 */
typedef struct PawingoFsrsState {
  /**
   * Updated model state.
   */
  struct PawingoFsrsMemory memory;
  /**
   * Unrounded interval in days; learning steps and queue policy belong to the host.
   */
  float interval_days;
} PawingoFsrsState;

/**
 * Ordered results for Again, Hard, Good and Easy (ratings 1 through 4).
 */
typedef struct PawingoFsrsNext {
  /**
   * Four fully initialized states on success; unchanged on failure.
   */
  struct PawingoFsrsState states[4];
} PawingoFsrsNext;

/**
 * One chronological review, independent of answer matching.
 */
typedef struct PawingoFsrsReview {
  /**
   * Again=1, Hard=2, Good=3, Easy=4.
   */
  uint32_t rating;
  /**
   * Whole elapsed days since the preceding review; zero for a new card's first review.
   */
  uint32_t elapsed_days;
} PawingoFsrsReview;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * Creates an immutable model, borrowing parameters only for this call. Zero parameters selects
 * upstream defaults; successful ownership transfers to the caller, otherwise `out` is null.
 *
 * # Safety
 * `out` must point to writable pointer storage. For nonzero `len`, `parameters` must point to
 * `len` initialized aligned floats, disjoint from `out`. No pointer is retained after return.
 */
uint32_t pawingo_fsrs_v1_new(const float *parameters, size_t len, struct PawingoFsrs **out);

/**
 * Releases a model; null is accepted.
 *
 * # Safety
 * A non-null model must be an unreleased pointer from `pawingo_fsrs_v1_new`. No calls may be
 * active or begin using this pointer while it is being freed.
 */
void pawingo_fsrs_v1_free(struct PawingoFsrs *model);

/**
 * Computes all four rating outcomes without mutating the model or previous state.
 * A null memory pointer denotes a new card, which requires zero elapsed days.
 *
 * # Safety
 * `model` must remain live during this call. `out` must be aligned writable storage disjoint
 * from all inputs; non-null memory must point to an initialized state. Calls may share a model.
 */
uint32_t pawingo_fsrs_v1_next(const struct PawingoFsrs *model,
                              const struct PawingoFsrsMemory *memory,
                              float retention,
                              uint32_t elapsed_days,
                              struct PawingoFsrsNext *out);

/**
 * Replays a nonempty chronological history. Starting state may be null for a new card.
 *
 * # Safety
 * `model` must remain live. `reviews` must point to `len` initialized records; `out` must point to
 * disjoint writable storage. Non-null starting state must point to initialized memory.
 */
uint32_t pawingo_fsrs_v1_memory(const struct PawingoFsrs *model,
                                const struct PawingoFsrsReview *reviews,
                                size_t len,
                                const struct PawingoFsrsMemory *starting,
                                struct PawingoFsrsMemory *out);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* PAWINGO_FSRS_H */
