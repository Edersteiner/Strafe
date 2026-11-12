#include "strafe/core/utils.h"

#include <fstream>
#include <sstream>
#include <vector>

#include <SDL3/SDL.h>

namespace strafe::utils {
std::string readTextFile(const std::string &file_path) {
  std::ifstream infile(file_path);
  if (infile.is_open()) {
    std::stringstream buffer;
    buffer << infile.rdbuf();
    const std::string output = buffer.str();
    infile.close();
    return output;
  }
  return {};
}

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

} // namespace strafe::utils
