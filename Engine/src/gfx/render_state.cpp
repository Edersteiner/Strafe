#include "strafe/gfx/render_state.h"

namespace strafe::gfx {

std::optional<BlendMode> parseBlendMode(std::string_view text) {
  if (text == "opaque") {
    return BlendMode::Opaque;
  }
  if (text == "alpha") {
    return BlendMode::Alpha;
  }
  if (text == "additive") {
    return BlendMode::Additive;
  }
  return std::nullopt;
}

std::optional<CullMode> parseCullMode(std::string_view text) {
  if (text == "back") {
    return CullMode::Back;
  }
  if (text == "front") {
    return CullMode::Front;
  }
  if (text == "none") {
    return CullMode::None;
  }
  return std::nullopt;
}

std::optional<DepthMode> parseDepthMode(std::string_view text) {
  if (text == "test_write") {
    return DepthMode::TestWrite;
  }
  if (text == "test") {
    return DepthMode::Test;
  }
  if (text == "off") {
    return DepthMode::Off;
  }
  return std::nullopt;
}

} // namespace strafe::gfx
