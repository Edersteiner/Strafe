#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <format>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

namespace strafe::log {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };

// Calls below this level compile to nothing.
#ifdef NDEBUG
inline constexpr Level kCompiledLevel = Level::Info;
#else
inline constexpr Level kCompiledLevel = Level::Trace;
#endif

void set_level(Level level);
Level get_level();

namespace detail {
extern std::atomic<Level> g_level;

// Captures the caller's location as a default argument, which a variadic
// function cannot take after its parameter pack.
template <typename... Args> struct FormatWithLocation {
  std::format_string<Args...> fmt;
  std::source_location location;

  template <typename T>
    requires std::convertible_to<const T &, std::string_view>
  consteval FormatWithLocation(
      const T &str,
      std::source_location loc = std::source_location::current())
      : fmt(str), location(loc) {}
};

void vwrite(Level level, std::source_location location, std::string_view fmt,
            std::format_args args);
} // namespace detail

// Args are deduced from the arguments only, like std::format_string.
template <typename... Args>
using Format = detail::FormatWithLocation<std::type_identity_t<Args>...>;

namespace detail {
template <Level L, typename... Args>
void write(Format<Args...> fmt, Args &&...args) {
  if constexpr (L >= kCompiledLevel) {
    if (L >= g_level.load(std::memory_order_relaxed)) {
      vwrite(L, fmt.location, fmt.fmt.get(), std::make_format_args(args...));
    }
  }
}
} // namespace detail

template <typename... Args>
void trace(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Trace>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Debug>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Info>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Warn>(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void error(Format<Args...> fmt, Args &&...args) {
  detail::write<Level::Error>(fmt, std::forward<Args>(args)...);
}

} // namespace strafe::log
