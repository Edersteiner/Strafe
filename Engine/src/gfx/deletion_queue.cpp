#include "strafe/gfx/deletion_queue.h"

#include <cassert>
#include <utility>

namespace strafe::gfx {

void DeletionQueue::push(uint64_t signal_value, std::function<void()> destroy_fn) {
  assert(entries_.empty() || signal_value >= entries_.back().signal_value);
  entries_.push_back({.destroy_fn = std::move(destroy_fn), .signal_value = signal_value});
}

void DeletionQueue::flush(uint64_t completed_value) {
  while (!entries_.empty() && entries_.front().signal_value <= completed_value) {
    std::function<void()> destroy_fn = std::move(entries_.front().destroy_fn);
    entries_.pop_front();
    destroy_fn();
  }
}

void DeletionQueue::flushAll() { flush(UINT64_MAX); }

} // namespace strafe::gfx
