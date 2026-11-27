#pragma once

#include "strafe/core/reflect.h"

#include <atomic>
#include <concepts>
#include <cstdint>
#include <format>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

namespace strafe::log {

enum class [[reflect]] Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };

// Calls below this level compile to nothing.
#ifdef STRAFE_DEBUG
inline constexpr Level kCompiledLevel = Level::Trace;
#else
inline constexpr Level kCompiledLevel = Level::Info;
#endif

void setLevel(Level level);
Level getLevel();

// One line of the log.
struct Record {
  Level level = Level::Info;
  // From print, so it has no location.
  bool plain = false;
  // Since the program started.
  double seconds = 0.0;
  std::string_view file;
  uint32_t line = 0;
  std::string_view message;
};

using Sink = void (*)(void *user, const Record &record);

// Calls sink with every line, on the thread that logged it, one line at a
// time. A sink must not log. Returns false when there is no room for it.
bool addSink(Sink sink, void *user);
void removeSink(Sink sink, void *user);

namespace detail {
extern std::atomic<Level> g_level;

// Captures the caller's location as a default argument, which a variadic
// function cannot take after its parameter pack.
template <typename... Args> struct FormatWithLocation {
  std::format_string<Args...> fmt;
  std::source_location location;

  // Implicit so a string literal at the call site converts into this type.
  template <typename T>
    requires std::convertible_to<const T &, std::string_view>
  consteval FormatWithLocation( // NOLINT(misc-explicit-constructor)
      const T &str, std::source_location loc = std::source_location::current())
      : fmt(str), location(loc) {}
};

void vwrite(Level level, std::source_location location, std::string_view fmt, std::format_args args);
void vprint(std::string_view fmt, std::format_args args);
} // namespace detail

// Args are deduced from the arguments only, like std::format_string.
template <typename... Args> using Format = detail::FormatWithLocation<std::type_identity_t<Args>...>;

namespace detail {
template <Level L, typename... Args> void write(Format<Args...> fmt, Args &&...args) {
  if constexpr (L >= kCompiledLevel) {
    if (L >= g_level.load(std::memory_order_relaxed)) {
      vwrite(L, fmt.location, fmt.fmt.get(), std::make_format_args(args...));
    }
  }
}
} // namespace detail

template <typename... Args> void trace(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Trace>(fmt, std::forward<Args>(args)...);
}

template <typename... Args> void debug(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Debug>(fmt, std::forward<Args>(args)...);
}

template <typename... Args> void info(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Info>(fmt, std::forward<Args>(args)...);
}

template <typename... Args> void warn(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Warn>(fmt, std::forward<Args>(args)...);
}

template <typename... Args> void error(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Error>(fmt, std::forward<Args>(args)...);
}

// Output that someone asked for, such as a console command's. Has no location
// and shows at every level.
template <typename... Args> void print(std::format_string<Args...> fmt, Args &&...args) {
  detail::vprint(fmt.get(), std::make_format_args(args...));
}

} // namespace strafe::log

#include "strafe/core/log.gen.h"
