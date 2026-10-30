#include "core/log.h"

#include <chrono>
#include <cstdio>
#include <iterator>
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
    {.name = "TRACE", .color = "\x1b[90m"},
    {.name = "DEBUG", .color = "\x1b[36m"},
    {.name = "INFO ", .color = "\x1b[32m"},
    {.name = "WARN ", .color = "\x1b[33m"},
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
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) !=
           0;
  }();
  return kConsole;
#else
  static const bool kTty = isatty(fileno(stderr));
  return kTty;
#endif
}

std::string_view fileName(const char *path) {
  const std::string_view view = path;
  const std::size_t slash = view.find_last_of("/\\");
  return slash == std::string_view::npos ? view : view.substr(slash + 1);
}

} // namespace

void setLevel(Level level) {
  detail::g_level.store(level, std::memory_order_relaxed);
}

Level getLevel() { return detail::g_level.load(std::memory_order_relaxed); }

void detail::vwrite(Level level, std::source_location location,
                    std::string_view fmt, std::format_args args) {
  // Reused per thread so steady-state logging does not allocate.
  thread_local std::string buffer;
  buffer.clear();

  const auto elapsed =
      std::chrono::duration<double>(Clock::now() - kStart).count();
  const LevelStyle &style = kStyles[static_cast<std::size_t>(level)];
  const bool color = useColor();

  auto out = std::back_inserter(buffer);
  std::format_to(out, "[{:10.3f}] ", elapsed);
  const std::string_view file = fileName(location.file_name());
  if (color) {
    std::format_to(out, "{}{}\x1b[0m \x1b[90m{}:{}\x1b[0m ", style.color,
                   style.name, file, location.line());
  } else {
    std::format_to(out, "{} {}:{} ", style.name, file, location.line());
  }
  std::vformat_to(out, fmt, args);
  buffer.push_back('\n');

  // One fwrite per line keeps lines from interleaving across threads.
  std::fwrite(buffer.data(), 1, buffer.size(), stderr);
}

} // namespace strafe::log
