#include "strafe/renderer.h"

#include "strafe/renderer/renderer.h"

#include <cassert>

namespace strafe {

renderer::Renderer *Renderer::renderer_ = nullptr;

renderer::Renderer &Renderer::instance() {
  assert(renderer_ && "Renderer is only usable from App::onInit until App::onShutdown returns");
  return *renderer_;
}

std::optional<renderer::MeshId>
Renderer::createMesh(std::span<const renderer::Vertex> vertices, std::span<const uint32_t> indices) {
  return instance().createMesh(vertices, indices);
}

renderer::MeshId Renderer::primitive(renderer::Primitive primitive) { return instance().primitive(primitive); }

std::optional<renderer::TextureId>
Renderer::createTexture(const renderer::TextureData &data, renderer::ColorSpace color_space) {
  return instance().createTexture(data, color_space);
}

renderer::TextureId Renderer::defaultTexture() { return instance().defaultTexture(); }

std::optional<gfx::ShaderId> Renderer::loadShader(std::string_view name) { return instance().loadShader(name); }

std::optional<renderer::MaterialId> Renderer::createMaterial(gfx::ShaderId shader) {
  return instance().createMaterial(shader);
}

bool Renderer::setParam(renderer::MaterialId material, std::string_view name, const renderer::MaterialParam &value) {
  return instance().setParam(material, name, value);
}

renderer::FrameDesc &Renderer::frame() { return instance().frame(); }

void Renderer::draw(const renderer::DrawItem &item) { instance().frame().draws.push_back(item); }

bool Renderer::renderObj(const std::string &path, renderer::MaterialId material, const Transform &transform) {
  return instance().renderObj(path, material, transform);
}

void Renderer::setDebugView(renderer::DebugView view) { instance().setDebugView(view); }

renderer::DebugView Renderer::debugView() { return instance().debugView(); }

} // namespace strafe
