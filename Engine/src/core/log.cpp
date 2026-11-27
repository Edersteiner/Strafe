#include "strafe/core/log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace strafe::log {

namespace detail {
std::atomic<Level> g_level{kCompiledLevel};
} // namespace detail

namespace {

using Clock = std::chrono::steady_clock;

const Clock::time_point kStart = Clock::now();

struct LevelStyle {
  std::string_view name;
  std::string_view color;
};

constexpr LevelStyle kStyles[] = {
    {.name = "TRACE", .color = "\x1b[90m"}, {.name = "DEBUG", .color = "\x1b[36m"},
    {.name = "INFO ", .color = "\x1b[32m"}, {.name = "WARN ", .color = "\x1b[33m"},
    {.name = "ERROR", .color = "\x1b[31m"},
};

bool useColor() {
#ifdef _WIN32
  static const bool kConsole = [] {
    const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
      return false;
    }
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
  }();
  return kConsole;
#else
  static const bool kTty = isatty(fileno(stderr));
  return kTty;
#endif
}

struct SinkEntry {
  Sink sink = nullptr;
  void *user = nullptr;
};

std::array<SinkEntry, 4> g_sinks;
std::mutex g_mutex;

// Writes the line and hands the same one to the sinks, so both keep one order.
void emit(const std::string &text, const Record &record) {
  const std::scoped_lock lock(g_mutex);
  std::fwrite(text.data(), 1, text.size(), stderr);
  for (const SinkEntry &entry : g_sinks) {
    if (entry.sink) {
      entry.sink(entry.user, record);
    }
  }
}

double elapsedSeconds() { return std::chrono::duration<double>(Clock::now() - kStart).count(); }

std::string_view fileName(const char *path) {
  const std::string_view view = path;
  const std::size_t slash = view.find_last_of("/\\");
  return slash == std::string_view::npos ? view : view.substr(slash + 1);
}

} // namespace

void setLevel(Level level) { detail::g_level.store(level, std::memory_order_relaxed); }

Level getLevel() { return detail::g_level.load(std::memory_order_relaxed); }

bool addSink(Sink sink, void *user) {
  const std::scoped_lock lock(g_mutex);
  const auto it = std::ranges::find(g_sinks, nullptr, &SinkEntry::sink);
  if (it == g_sinks.end()) {
    return false;
  }
  *it = {.sink = sink, .user = user};
  return true;
}

void removeSink(Sink sink, void *user) {
  const std::scoped_lock lock(g_mutex);
  for (SinkEntry &entry : g_sinks) {
    if (entry.sink == sink && entry.user == user) {
      entry = {};
    }
  }
}

void detail::vwrite(Level level, std::source_location location, std::string_view fmt, std::format_args args) {
  // Reused per thread so steady-state logging does not allocate.
  thread_local std::string buffer;
  buffer.clear();

  const double elapsed = elapsedSeconds();
  const LevelStyle &style = kStyles[static_cast<std::size_t>(level)];
  const bool color = useColor();

  auto out = std::back_inserter(buffer);
  std::format_to(out, "[{:10.3f}] ", elapsed);
  const std::string_view file = fileName(location.file_name());
  if (color) {
    std::format_to(out, "{}{}\x1b[0m \x1b[90m{}:{}\x1b[0m ", style.color, style.name, file, location.line());
  } else {
    std::format_to(out, "{} {}:{} ", style.name, file, location.line());
  }
  const std::size_t message_start = buffer.size();
  std::vformat_to(out, fmt, args);
  const std::size_t message_size = buffer.size() - message_start;
  buffer.push_back('\n');

  emit(
      buffer,
      {.level = level,
       .seconds = elapsed,
       .file = file,
       .line = location.line(),
       .message = std::string_view(buffer).substr(message_start, message_size)});
}

void detail::vprint(std::string_view fmt, std::format_args args) {
  thread_local std::string buffer;
  buffer.clear();

  const double elapsed = elapsedSeconds();
  auto out = std::back_inserter(buffer);
  std::format_to(out, "[{:10.3f}] ", elapsed);
  const std::size_t message_start = buffer.size();
  std::vformat_to(out, fmt, args);
  const std::size_t message_size = buffer.size() - message_start;
  buffer.push_back('\n');

  emit(
      buffer,
      {.plain = true, .seconds = elapsed, .message = std::string_view(buffer).substr(message_start, message_size)});
}

} // namespace strafe::log
