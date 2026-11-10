#include "renderer/texture_loader.h"

#include "core/log.h"
#include "core/utils.h"

#include <cstring>
#include <memory>
#include <string>

#include <stb_image.h>

namespace strafe::renderer {

std::optional<TextureData> loadTexture(const char *file_path) {
  const std::string path = utils::basePath() + file_path;

  constexpr int kChannels = 4;
  int width = 0;
  int height = 0;
  int file_channels = 0;
  // Freed by stbi_image_free however this function returns.
  const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
      stbi_load(path.c_str(), &width, &height, &file_channels, kChannels),
      &stbi_image_free);
  if (!pixels) {
    log::error("Failed to load texture {}: {}", path, stbi_failure_reason());
    return std::nullopt;
  }

  TextureData texture{.width = static_cast<uint32_t>(width),
                      .height = static_cast<uint32_t>(height)};
  texture.rgba.resize(static_cast<std::size_t>(width) * height * kChannels);
  std::memcpy(texture.rgba.data(), pixels.get(), texture.rgba.size());

  log::info("Loaded {}: {}x{}, {} channels in file", file_path, width, height,
            file_channels);
  return texture;
}

} // namespace strafe::renderer
