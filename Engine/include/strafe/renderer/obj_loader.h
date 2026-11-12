#pragma once

#include "strafe/renderer/mesh.h"

namespace strafe::renderer {

std::optional<MeshData> loadFromObj(const char *file_path);

} // namespace strafe::renderer
