#include "strafe/console.h"

#include "console/console_manager.h"
#include "input/input_manager.h"

#include <cassert>
#include <format>
#include <iterator>
#include <utility>

namespace strafe::console {

void ConsoleManager::initialize(input::InputManager &input) {
  input_ = &input;
  if (!log::addSink(&ConsoleManager::onLog, this)) {
    log::warn("The console can't show the log, because there are too many log sinks");
  }
}

void ConsoleManager::shutdown() { log::removeSink(&ConsoleManager::onLog, this); }

void ConsoleManager::onLog(void *user, const log::Record &record) {
  auto &console = *static_cast<ConsoleManager *>(user);
  const std::string location = record.plain ? std::string() : std::format("{}:{}", record.file, record.line);

  const std::scoped_lock lock(console.pending_mutex_);
  // One entry per line of text, so they all have the same height.
  size_t start = 0;
  while (start <= record.message.size()) {
    const size_t end = std::min(record.message.find('\n', start), record.message.size());
    console.pending_.push_back(
        {.level = record.level,
         .plain = record.plain,
         .seconds = record.seconds,
         .location = location,
         .text = std::string(record.message.substr(start, end - start))});
    start = end + 1;
  }
  while (console.pending_.size() > kMaxLines) {
    console.pending_.pop_front();
  }
}

void ConsoleManager::collect() {
  std::deque<Line> pending;
  {
    const std::scoped_lock lock(pending_mutex_);
    pending.swap(pending_);
  }
  lines_.insert(lines_.end(), std::move_iterator(pending.begin()), std::move_iterator(pending.end()));
  while (lines_.size() > kMaxLines) {
    lines_.pop_front();
  }
}

void ConsoleManager::setOpen(bool open) {
  if (open_ == open) {
    return;
  }
  open_ = open;
  focus_input_ = open;
  input_->setBlocked(open);
}

void ConsoleManager::submit(std::string_view line) { submitted_.emplace_back(line); }

void ConsoleManager::clear() {
  collect();
  lines_.clear();
}

void ConsoleManager::update() {
  // A command that submits another has it run next frame.
  for (const std::string &line : std::exchange(submitted_, {})) {
    log::print("> {}", line);
    commands_.run(line);
  }
  collect();
}

} // namespace strafe::console

namespace strafe {

console::ConsoleManager *Console::manager_ = nullptr;

console::ConsoleManager &Console::manager() {
  assert(manager_ && "Console is only usable from App::onInit until App::onShutdown returns");
  return *manager_;
}

console::CommandRegistry &Console::registry() { return manager().commands(); }

bool Console::isOpen() { return manager().isOpen(); }

void Console::open() { manager().setOpen(true); }

void Console::close() { manager().setOpen(false); }

void Console::execute(std::string_view line) { manager().submit(line); }

void Console::clear() { manager().clear(); }

void Console::removeCommand(std::string_view name) { registry().remove(name); }

const console::CommandRegistry &Console::commands() { return registry(); }

} // namespace strafe
