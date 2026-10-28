use super::*;

struct Model(*mut PawingoFsrs);

impl Model {
    fn new() -> Self {
        let mut model = std::ptr::null_mut();
        assert_eq!(
            // SAFETY: Empty parameter slice and writable local output satisfy the C contract.
            unsafe { pawingo_fsrs_v1_new(std::ptr::null(), 0, &mut model) },
            PAWINGO_FSRS_OK
        );
        Self(model)
    }
}

impl Drop for Model {
    fn drop(&mut self) {
        // SAFETY: This guard is the unique owner and no calls outlive it.
        unsafe { pawingo_fsrs_v1_free(self.0) };
    }
}

#[test]
fn new_card_outcomes_match_upstream_for_every_rating() {
    let model = Model::new();
    let mut actual = PawingoFsrsNext::default();
    assert_eq!(
        // SAFETY: The model is live and output is disjoint writable storage.
        unsafe { pawingo_fsrs_v1_next(model.0, std::ptr::null(), 0.9, 0, &mut actual) },
        PAWINGO_FSRS_OK
    );
    let expected = FSRS::default().next_states(None, 0.9, 0).unwrap();
    for (actual, expected) in
        actual
            .states
            .iter()
            .zip([expected.again, expected.hard, expected.good, expected.easy])
    {
        assert_eq!(
            *actual,
            PawingoFsrsState {
                memory: expected.memory.into(),
                interval_days: expected.interval
            }
        );
    }
}

#[test]
fn invalid_retention_leaves_output_unchanged() {
    let model = Model::new();
    let mut actual = PawingoFsrsNext::default();
    // SAFETY: The model is live and output is disjoint writable storage.
    let status =
        unsafe { pawingo_fsrs_v1_next(model.0, std::ptr::null(), f32::NAN, 0, &mut actual) };
    assert_eq!(
        (status, actual),
        (PAWINGO_FSRS_INVALID_INPUT, PawingoFsrsNext::default())
    );
}

#[test]
fn invalid_parameters_do_not_transfer_ownership() {
    let mut model = std::ptr::null_mut();
    let parameters = [f32::NAN; 21];
    // SAFETY: Both the parameter slice and output pointer are valid and disjoint.
    let status = unsafe { pawingo_fsrs_v1_new(parameters.as_ptr(), parameters.len(), &mut model) };
    assert_eq!(
        (status, model.is_null()),
        (PAWINGO_FSRS_INVALID_PARAMETERS, true)
    );
}

#[test]
fn missing_output_pointer_is_rejected() {
    assert_eq!(
        // SAFETY: Null pointers are explicitly rejected without dereferencing them.
        unsafe { pawingo_fsrs_v1_new(std::ptr::null(), 0, std::ptr::null_mut()) },
        PAWINGO_FSRS_INVALID_INPUT
    );
}

#[test]
fn invalid_history_rating_is_rejected() {
    let model = Model::new();
    let reviews = [PawingoFsrsReview {
        rating: 5,
        elapsed_days: 0,
    }];
    let mut memory = PawingoFsrsMemory::default();
    assert_eq!(
        // SAFETY: Model, history and disjoint output remain valid for the call.
        unsafe {
            pawingo_fsrs_v1_memory(model.0, reviews.as_ptr(), 1, std::ptr::null(), &mut memory)
        },
        PAWINGO_FSRS_INVALID_INPUT
    );
}

#[test]
fn replay_matches_upstream_memory() {
    let model = Model::new();
    let reviews = [
        PawingoFsrsReview {
            rating: 3,
            elapsed_days: 0,
        },
        PawingoFsrsReview {
            rating: 4,
            elapsed_days: 5,
        },
    ];
    let mut memory = PawingoFsrsMemory::default();
    assert_eq!(
        // SAFETY: Model, history and disjoint output remain valid for the call.
        unsafe {
            pawingo_fsrs_v1_memory(
                model.0,
                reviews.as_ptr(),
                reviews.len(),
                std::ptr::null(),
                &mut memory,
            )
        },
        PAWINGO_FSRS_OK
    );
    let expected = FSRS::default()
        .memory_state(
            FSRSItem {
                reviews: reviews
                    .iter()
                    .map(|r| FSRSReview {
                        rating: r.rating,
                        delta_t: r.elapsed_days,
                    })
                    .collect(),
            },
            None,
        )
        .unwrap();
    assert_eq!(memory, expected.into());
}

#[test]
fn unwinding_panic_becomes_status_code() {
    assert_eq!(
        boundary(|| panic!("injected boundary failure")),
        PAWINGO_FSRS_PANIC
    );
}

#[test]
fn custom_parameters_are_copied_and_match_upstream() {
    let mut parameters = fsrs::DEFAULT_PARAMETERS;
    parameters[2] = 4.5;
    let expected = FSRS::new(&parameters)
        .unwrap()
        .next_states(None, 0.9, 0)
        .unwrap();
    let mut raw = std::ptr::null_mut();
    // SAFETY: The slice and disjoint output pointer are valid for construction.
    let status = unsafe { pawingo_fsrs_v1_new(parameters.as_ptr(), parameters.len(), &mut raw) };
    assert_eq!(status, PAWINGO_FSRS_OK);
    let model = Model(raw);
    parameters.fill(f32::NAN);
    let mut actual = PawingoFsrsNext::default();
    // SAFETY: The owning guard keeps the model alive and output is disjoint storage.
    let status = unsafe { pawingo_fsrs_v1_next(model.0, std::ptr::null(), 0.9, 0, &mut actual) };
    assert_eq!(
        (status, actual.states[2].memory),
        (PAWINGO_FSRS_OK, expected.good.memory.into())
    );
}
