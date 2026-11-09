#pragma once

#include <string>
#include <vector>

namespace strafe::utils {

std::string readTextFile(const std::string &file_path);
std::string basePath();

std::vector<std::string_view> split(std::string_view s, char delim = ' ');
} // namespace strafe::utils
