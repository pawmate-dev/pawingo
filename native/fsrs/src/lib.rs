//! Private, versioned C boundary for FSRS scheduling. All returned data is caller-owned.
//! Invalid pointers remain a caller error; ordinary validation failures and unwinding panics are
//! converted to status codes. Allocation aborts and undefined behavior cannot be recovered here.

use std::panic::{AssertUnwindSafe, catch_unwind};

use fsrs::{FSRS, FSRSItem, FSRSReview, MemoryState};

/// Success.
pub const PAWINGO_FSRS_OK: u32 = 0;
/// Invalid pointer, reading history, memory state, elapsed time or retention.
pub const PAWINGO_FSRS_INVALID_INPUT: u32 = 1;
/// Unsupported or non-finite model parameters.
pub const PAWINGO_FSRS_INVALID_PARAMETERS: u32 = 2;
/// The model produced an invalid result.
pub const PAWINGO_FSRS_BACKEND_ERROR: u32 = 3;
/// An unexpected unwinding panic was contained at the boundary.
pub const PAWINGO_FSRS_PANIC: u32 = 4;

/// Opaque immutable model, exclusively owned by its C++ RAII wrapper.
pub struct PawingoFsrs(FSRS);

/// Model state; stability is measured in days and difficulty ranges from 1 to 10.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PawingoFsrsMemory {
    /// Stability in days; must be positive and finite.
    pub stability: f32,
    /// Difficulty in the inclusive range [1, 10].
    pub difficulty: f32,
}

/// Result for one of the four ratings.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PawingoFsrsState {
    /// Updated model state.
    pub memory: PawingoFsrsMemory,
    /// Unrounded interval in days; learning steps and queue policy belong to the host.
    pub interval_days: f32,
}

/// Ordered results for Again, Hard, Good and Easy (ratings 1 through 4).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PawingoFsrsNext {
    /// Four fully initialized states on success; unchanged on failure.
    pub states: [PawingoFsrsState; 4],
}

/// One chronological review, independent of answer matching.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct PawingoFsrsReview {
    /// Again=1, Hard=2, Good=3, Easy=4.
    pub rating: u32,
    /// Whole elapsed days since the preceding review; zero for a new card's first review.
    pub elapsed_days: u32,
}

fn boundary(work: impl FnOnce() -> Result<(), u32>) -> u32 {
    match catch_unwind(AssertUnwindSafe(work)) {
        Ok(Ok(())) => PAWINGO_FSRS_OK,
        Ok(Err(status)) => status,
        Err(_) => PAWINGO_FSRS_PANIC,
    }
}

fn validate_memory(state: PawingoFsrsMemory) -> Result<MemoryState, u32> {
    if !state.stability.is_finite()
        || state.stability <= 0.0
        || !state.difficulty.is_finite()
        || !(1.0..=10.0).contains(&state.difficulty)
    {
        return Err(PAWINGO_FSRS_INVALID_INPUT);
    }
    Ok(MemoryState {
        stability: state.stability,
        difficulty: state.difficulty,
    })
}

impl From<MemoryState> for PawingoFsrsMemory {
    fn from(value: MemoryState) -> Self {
        Self {
            stability: value.stability,
            difficulty: value.difficulty,
        }
    }
}

/// Creates an immutable model, borrowing parameters only for this call. Zero parameters selects
/// upstream defaults; successful ownership transfers to the caller, otherwise `out` is null.
///
/// # Safety
/// `out` must point to writable pointer storage. For nonzero `len`, `parameters` must point to
/// `len` initialized aligned floats, disjoint from `out`. No pointer is retained after return.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pawingo_fsrs_v1_new(
    parameters: *const f32,
    len: usize,
    out: *mut *mut PawingoFsrs,
) -> u32 {
    boundary(|| {
        if out.is_null() {
            return Err(PAWINGO_FSRS_INVALID_INPUT);
        }
        // SAFETY: Caller guarantees writable output storage.
        unsafe { *out = std::ptr::null_mut() };
        if !matches!(len, 0 | 17 | 19 | 21) {
            return Err(PAWINGO_FSRS_INVALID_PARAMETERS);
        }
        let parameters = if len == 0 {
            &[]
        } else {
            if parameters.is_null() {
                return Err(PAWINGO_FSRS_INVALID_INPUT);
            }
            // SAFETY: Caller guarantees an initialized slice of this length.
            unsafe { std::slice::from_raw_parts(parameters, len) }
        };
        let model = FSRS::new(parameters).map_err(|_| PAWINGO_FSRS_INVALID_PARAMETERS)?;
        // SAFETY: Caller guarantees writable output storage; ownership now passes to C++.
        unsafe { *out = Box::into_raw(Box::new(PawingoFsrs(model))) };
        Ok(())
    })
}

/// Releases a model; null is accepted.
///
/// # Safety
/// A non-null model must be an unreleased pointer from `pawingo_fsrs_v1_new`. No calls may be
/// active or begin using this pointer while it is being freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pawingo_fsrs_v1_free(model: *mut PawingoFsrs) {
    if !model.is_null() {
        // SAFETY: Caller transfers unique ownership of the Box allocation back to Rust.
        unsafe { drop(Box::from_raw(model)) };
    }
}

/// Computes all four rating outcomes without mutating the model or previous state.
/// A null memory pointer denotes a new card, which requires zero elapsed days.
///
/// # Safety
/// `model` must remain live during this call. `out` must be aligned writable storage disjoint
/// from all inputs; non-null memory must point to an initialized state. Calls may share a model.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pawingo_fsrs_v1_next(
    model: *const PawingoFsrs,
    memory: *const PawingoFsrsMemory,
    retention: f32,
    elapsed_days: u32,
    out: *mut PawingoFsrsNext,
) -> u32 {
    boundary(|| {
        if model.is_null()
            || out.is_null()
            || !retention.is_finite()
            || !(0.0..1.0).contains(&retention)
            || retention == 0.0
        {
            return Err(PAWINGO_FSRS_INVALID_INPUT);
        }
        let memory = if memory.is_null() {
            if elapsed_days != 0 {
                return Err(PAWINGO_FSRS_INVALID_INPUT);
            }
            None
        } else {
            // SAFETY: Caller guarantees initialized memory when non-null.
            Some(validate_memory(unsafe { *memory })?)
        };
        // SAFETY: Caller keeps the immutable model alive for the duration of this call.
        let next = unsafe { &*model }
            .0
            .next_states(memory, retention, elapsed_days)
            .map_err(|_| PAWINGO_FSRS_BACKEND_ERROR)?;
        let mut result = PawingoFsrsNext::default();
        for (out, state) in result
            .states
            .iter_mut()
            .zip([next.again, next.hard, next.good, next.easy])
        {
            validate_memory(state.memory.into()).map_err(|_| PAWINGO_FSRS_BACKEND_ERROR)?;
            if !state.interval.is_finite() || state.interval <= 0.0 {
                return Err(PAWINGO_FSRS_BACKEND_ERROR);
            }
            *out = PawingoFsrsState {
                memory: state.memory.into(),
                interval_days: state.interval,
            };
        }
        // SAFETY: Caller provides disjoint writable output; commit only the complete result.
        unsafe { *out = result };
        Ok(())
    })
}

/// Replays a nonempty chronological history. Starting state may be null for a new card.
///
/// # Safety
/// `model` must remain live. `reviews` must point to `len` initialized records; `out` must point to
/// disjoint writable storage. Non-null starting state must point to initialized memory.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pawingo_fsrs_v1_memory(
    model: *const PawingoFsrs,
    reviews: *const PawingoFsrsReview,
    len: usize,
    starting: *const PawingoFsrsMemory,
    out: *mut PawingoFsrsMemory,
) -> u32 {
    boundary(|| {
        if model.is_null() || reviews.is_null() || out.is_null() || len == 0 || len > 1_000_000 {
            return Err(PAWINGO_FSRS_INVALID_INPUT);
        }
        let starting = if starting.is_null() {
            None
        } else {
            // SAFETY: Caller guarantees initialized starting state.
            Some(validate_memory(unsafe { *starting })?)
        };
        // SAFETY: Caller guarantees an initialized record slice of bounded length.
        let reviews = unsafe { std::slice::from_raw_parts(reviews, len) };
        if starting.is_none() && reviews[0].elapsed_days != 0 {
            return Err(PAWINGO_FSRS_INVALID_INPUT);
        }
        let reviews = reviews
            .iter()
            .map(|review| {
                if !(1..=4).contains(&review.rating) {
                    return Err(PAWINGO_FSRS_INVALID_INPUT);
                }
                Ok(FSRSReview {
                    rating: review.rating,
                    delta_t: review.elapsed_days,
                })
            })
            .collect::<Result<Vec<_>, _>>()?;
        // SAFETY: Caller keeps the immutable model alive for this call.
        let memory = unsafe { &*model }
            .0
            .memory_state(FSRSItem { reviews }, starting)
            .map_err(|_| PAWINGO_FSRS_BACKEND_ERROR)?;
        validate_memory(memory.into()).map_err(|_| PAWINGO_FSRS_BACKEND_ERROR)?;
        // SAFETY: Caller guarantees disjoint writable output; failure leaves it unchanged.
        unsafe { *out = memory.into() };
        Ok(())
    })
}

#[cfg(test)]
mod tests;
