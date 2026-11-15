#pragma once

#include "strafe/renderer/texture.h"

#include <optional>

namespace strafe::renderer {

// Decodes a PNG, JPEG, TGA or BMP relative to the executable, the same as
// loadFromObj. Every image is expanded to RGBA.
std::optional<TextureData> loadTexture(const char *file_path);

} // namespace strafe::renderer
