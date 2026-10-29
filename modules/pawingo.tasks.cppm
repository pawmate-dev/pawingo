export module pawingo.tasks;

import std;

/// Frontend-neutral ownership for cancellable background work.
export namespace pawingo::tasks {
/// Owns at most one worker and joins it before releasing its callbacks.
class BackgroundJob {
public:
  using Work = std::function<void(std::stop_token)>;
  /// Receives a task exception, or nullptr after successful work, on the worker thread.
  using Completion = std::function<void(std::exception_ptr)>;
  BackgroundJob() = default;

  ~BackgroundJob() {
    cancel_and_wait();
  }

  BackgroundJob(const BackgroundJob &) = delete;
  BackgroundJob &operator=(const BackgroundJob &) = delete;

  /// Returns whether work or its completion callback is still executing.
  bool running() const {
    return running_.load();
  }

  /// Starts a task without blocking the caller on its work.
  ///
  /// @param work Blocking operation that cooperatively observes its stop token.
  /// @param complete Receives the outcome on the worker; must not throw or reenter this job.
  /// @return False when an earlier task is still active; otherwise true.
  /// @throws std::invalid_argument If either callback is empty.
  /// @throws std::system_error If the worker thread cannot be created.
  ///
  /// Call from one owner thread. State captured by either callback must outlive this object.
  /// Worker exceptions are delivered to complete rather than escaping the thread entry point.
  bool start(Work work, Completion complete) {
    if (!work || !complete)
      throw std::invalid_argument("Background callbacks must not be empty.");
    if (running_.exchange(true))
      return false;
    try {
      worker_ = std::jthread(
        [this, work = std::move(work), complete = std::move(complete)](std::stop_token stop) {
          std::exception_ptr failure;
          try {
            work(stop);
          } catch (...) {
            failure = std::current_exception();
          }
          complete(failure);
          running_ = false;
        }
      );
    } catch (...) {
      running_ = false;
      throw;
    }
    return true;
  }

  /// Requests cancellation without blocking the owner thread; destruction still joins the worker.
  void request_stop() {
    if (worker_.joinable())
      worker_.request_stop();
  }

  /// Requests cooperative cancellation and waits for the worker to finish.
  /// Call from the owner thread, never from a task or its completion callback.
  void cancel_and_wait() {
    if (worker_.joinable()) {
      worker_.request_stop();
      worker_.join();
    }
  }

private:
  std::atomic<bool> running_{false};
  std::jthread worker_;
};
} // namespace pawingo::tasks
