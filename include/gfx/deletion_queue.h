#pragma once

#include <cstdint>
#include <deque>
#include <functional>

namespace strafe::gfx {

class DeletionQueue {
public:
  DeletionQueue() = default;
  ~DeletionQueue() = default;

  DeletionQueue(const DeletionQueue &) = delete;
  DeletionQueue &operator=(const DeletionQueue &) = delete;

  // Push a function to be called when the signal value is reached.
  // If a push has a lower value than the preceding push,
  // it will be stuck behind the preceding push and called later.
  void push(uint64_t signal_value, std::function<void()> destroy_fn);

  // Runs all deletion functions with a signal value
  // less than or equal to the completed value.
  void flush(uint64_t completed_value);
  // Runs all deletion functions. Only safe when GPU is idle.
  void flushAll();

private:
  struct Entry {
    std::function<void()> destroy_fn;
    uint64_t signal_value = 0;
  };

  std::deque<Entry> entries_;
};

} // namespace strafe::gfx
