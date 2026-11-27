#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace strafe::utils {

std::string basePath();

std::vector<std::string_view> split(std::string_view s, char delim = ' ');

// ASCII only.
std::string toLower(std::string_view text);

// The candidate name is most likely a typo of, ignoring case, if any is close.
std::optional<std::string_view> closestMatch(std::string_view name, std::span<const std::string_view> candidates);

} // namespace strafe::utils
