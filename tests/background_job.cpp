import std;
import pawingo.tasks;

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void active_job_should_reject_overlapping_work_and_observe_cancellation() {
  pawingo::tasks::BackgroundJob job;
  std::binary_semaphore started{0};
  bool cancelled = false;
  require(job.start(
            [&](std::stop_token stop) {
              started.release();
              while (!stop.stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
              cancelled = true;
            },
            [](std::exception_ptr failure) {
              if (failure)
                std::terminate();
            }
          ),
    "first task starts");
  started.acquire();
  require(job.running(), "worker is active");
  require(!job.start(
            [](std::stop_token) {
            },
            [](std::exception_ptr) {
            }
          ),
    "overlapping task is rejected");
  job.cancel_and_wait();
  require(cancelled && !job.running(), "cancel joins the worker");
}

void work_failure_should_reach_completion_without_escaping_the_thread() {
  pawingo::tasks::BackgroundJob job;
  bool delivered = false;
  job.start(
    [](std::stop_token) {
      throw std::runtime_error("fixture failure");
    },
    [&](std::exception_ptr failure) {
      try {
        if (failure)
          std::rethrow_exception(failure);
      } catch (const std::runtime_error &) {
        delivered = true;
      }
    }
  );
  job.cancel_and_wait();
  require(delivered, "task exception is delivered to owner callback");
}
} // namespace

int main() {
  try {
    active_job_should_reject_overlapping_work_and_observe_cancellation();
    work_failure_should_reach_completion_without_escaping_the_thread();
  } catch (const std::exception &error) {
    std::cout << error.what() << std::endl;
    return 1;
  }
}
