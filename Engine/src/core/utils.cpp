#include "strafe/core/utils.h"

#include <algorithm>
#include <vector>

#include <SDL3/SDL.h>

namespace strafe::utils {

namespace {

size_t editDistance(std::string_view a, std::string_view b) {
  std::vector<size_t> row(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) {
    row[j] = j;
  }
  for (size_t i = 1; i <= a.size(); ++i) {
    size_t diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t above = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
      diagonal = above;
    }
  }
  return row[b.size()];
}

} // namespace

std::string basePath() { return SDL_GetBasePath(); }

std::vector<std::string_view> split(std::string_view s, char delim) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start < s.size()) {
    size_t end = s.find(delim, start);
    if (end == std::string_view::npos) {
      end = s.size();
    }
    if (end > start) {
      out.push_back(s.substr(start, end - start));
    }
    start = end + 1;
  }
  return out;
}

std::string toLower(std::string_view text) {
  std::string result(text);
  std::ranges::transform(
      result, result.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
  return result;
}

std::optional<std::string_view> closestMatch(std::string_view name, std::span<const std::string_view> candidates) {
  const std::string lowered = toLower(name);
  // One edit, or a third of the name.
  const size_t limit = std::max<size_t>(1, name.size() / 3);
  std::optional<std::string_view> best;
  size_t best_distance = limit + 1;
  for (const std::string_view candidate : candidates) {
    const size_t distance = editDistance(lowered, toLower(candidate));
    if (distance < best_distance) {
      best = candidate;
      best_distance = distance;
    }
  }
  return best;
}

} // namespace strafe::utils
