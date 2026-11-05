#include "demo/week2_scene.h"

#include "demo/sphere.h"
#include "renderer/camera.h"
#include "renderer/mesh.h"
#include "renderer/renderer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace strafe::demo {

namespace {
// Counter-clockwise when seen from +Z in a Y-up world
constexpr std::array kPyramidVertices{
    renderer::Vertex{.position = {-0.5f, -0.5f, 0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.5f, -0.5f, 0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.5f, -0.5f, -0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {-0.5f, -0.5f, -0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.0f, 0.5f, 0.0f}, .color = {1, 1, 1}}};
constexpr std::array<uint32_t, 18> kPyramidIndices{
    0, 1, 4, // front (+Z)
    1, 2, 4, // right (+X)
    2, 3, 4, // back  (-Z)
    3, 0, 4, // left  (-X)
    0, 3, 2, // base  (-Y)
    0, 2, 1,
};

// Corners 0 to 3 are the +Z face and 4 to 7 the -Z face, both listed
// bottom left, bottom right, top right, top left as seen from +Z.
constexpr std::array kCubeVertices{
    renderer::Vertex{.position = {-0.5f, -0.5f, 0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.5f, -0.5f, 0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.5f, 0.5f, 0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {-0.5f, 0.5f, 0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {-0.5f, -0.5f, -0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.5f, -0.5f, -0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.5f, 0.5f, -0.5f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {-0.5f, 0.5f, -0.5f}, .color = {1, 1, 1}}};
// Two triangles per face, counter-clockwise as seen from outside the cube.
constexpr std::array<uint32_t, 36> kCubeIndices{
    0, 1, 2, 0, 2, 3, // front  (+Z)
    5, 4, 7, 5, 7, 6, // back   (-Z)
    1, 5, 6, 1, 6, 2, // right  (+X)
    4, 0, 3, 4, 3, 7, // left   (-X)
    3, 2, 6, 3, 6, 7, // top    (+Y)
    4, 5, 1, 4, 1, 0, // bottom (-Y)
};

constexpr int kSphereRings = 16;
constexpr int kSphereSlices = 32;

constexpr glm::vec3 kRestingCubePosition(-2.2f, 0.3f, 0.8f);
constexpr glm::vec3 kRestingSpherePosition(0.2f, -0.6f, 1.4f);
constexpr glm::vec3 kRestingPyramidPosition(2.4f, -0.4f, 0.2f);
constexpr glm::vec3 kSpinningCubePosition(1.6f, 0.9f, -2.5f);
constexpr glm::vec3 kCirclingSpherePosition(-1.0f, 1.2f, -3.5f);
constexpr glm::vec3 kPulsingPyramidPosition(-0.2f, 0.4f, -1.2f);

constexpr float kSpinSpeed = 1.0f;   // Radians per second.
constexpr float kOrbitRadius = 0.6f; // How far the moving sphere strays.
constexpr float kOrbitSpeed = 1.5f;  // Radians per second.
constexpr float kPulseSpeed = 2.0f;  // Radians per second.
constexpr float kPulseMinScale = 0.6f;
constexpr float kPulseMaxScale = 1.2f;

constexpr glm::vec3 kCameraPosition(0.0f, 2.0f, 6.5f);
constexpr glm::vec3 kCameraTarget(0.0f, 0.2f, -1.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kOrthographicHeight = 6.0f;

// The sphere is generated with radius 1, so half scale matches the unit cube.
constexpr float kSphereScale = 0.5f;

void drawMesh(renderer::FrameDesc &frame, renderer::MeshId mesh,
              const glm::mat4 &transform, glm::vec4 color) {
  frame.draws.push_back({.mesh = mesh, .transform = transform, .color = color});
}

glm::mat4 translation(glm::vec3 position) {
  return glm::translate(glm::mat4(1.0f), position);
}
} // namespace

bool createSceneMeshes(renderer::Renderer &renderer, Scene &scene) {
  const std::optional<renderer::MeshId> cube =
      renderer.createMesh(kCubeVertices, kCubeIndices);
  const std::optional<renderer::MeshId> pyramid =
      renderer.createMesh(kPyramidVertices, kPyramidIndices);

  std::vector<renderer::Vertex> sphere_vertices;
  std::vector<uint32_t> sphere_indices;
  if (!generateSphere(kSphereRings, kSphereSlices, sphere_vertices,
                      sphere_indices)) {
    return false;
  }
  const std::optional<renderer::MeshId> sphere =
      renderer.createMesh(sphere_vertices, sphere_indices);

  if (!cube || !pyramid || !sphere) {
    return false;
  }
  scene.cube = *cube;
  scene.pyramid = *pyramid;
  scene.sphere = *sphere;
  return true;
}

void toggleProjection(Scene &scene) { scene.perspective = !scene.perspective; }

void updateScene(Scene &scene, float delta_seconds) {
  scene.time += delta_seconds;
}

void buildScene(renderer::FrameDesc &frame, const Scene &scene, float aspect) {
  const glm::mat4 view =
      glm::lookAt(kCameraPosition, kCameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 projection =
      scene.perspective ? renderer::perspective(glm::radians(60.0f), aspect,
                                                kNearPlane, kFarPlane)
                        : renderer::orthographic(kOrthographicHeight, aspect,
                                                 kNearPlane, kFarPlane);
  frame.view_projection = projection * view;

  const glm::vec4 cube_color(0.2f, 0.4f, 0.9f, 1.0f);
  const glm::vec4 sphere_color(0.2f, 0.8f, 0.3f, 1.0f);
  const glm::vec4 pyramid_color(0.9f, 0.2f, 0.2f, 1.0f);

  // Resting shapes, turned so more than one face shows.
  glm::mat4 cube = translation(kRestingCubePosition);
  cube = glm::rotate(cube, glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
  cube = glm::rotate(cube, glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  drawMesh(frame, scene.cube, cube, cube_color);

  drawMesh(
      frame, scene.sphere,
      glm::scale(translation(kRestingSpherePosition), glm::vec3(kSphereScale)),
      sphere_color);

  drawMesh(frame, scene.pyramid,
           glm::rotate(translation(kRestingPyramidPosition),
                       glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
           pyramid_color);

  // Animated shapes.
  const glm::vec3 spin_axis = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
  drawMesh(frame, scene.cube,
           glm::rotate(translation(kSpinningCubePosition),
                       scene.time * kSpinSpeed, spin_axis),
           cube_color);

  // Circles in the XZ plane, so it moves towards and away from the camera and
  // only changes size under perspective.
  const float orbit = scene.time * kOrbitSpeed;
  const glm::vec3 orbit_offset =
      kOrbitRadius * glm::vec3(std::cos(orbit), 0.0f, std::sin(orbit));
  drawMesh(frame, scene.sphere,
           glm::scale(translation(kCirclingSpherePosition + orbit_offset),
                      glm::vec3(kSphereScale)),
           sphere_color);

  // Maps sin's -1 to 1 onto the min to max scale.
  const float pulse = 0.5f + 0.5f * std::sin(scene.time * kPulseSpeed);
  const float pulse_scale =
      kPulseMinScale + pulse * (kPulseMaxScale - kPulseMinScale);
  drawMesh(
      frame, scene.pyramid,
      glm::scale(glm::rotate(translation(kPulsingPyramidPosition),
                             glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                 glm::vec3(pulse_scale)),
      pyramid_color);
}

} // namespace strafe::demo
