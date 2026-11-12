set_project("Strafe")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate")

-- Build every dependency from xmake-repo so all platforms use the same versions.
add_requireconfs("*", { system = false })

-- volk and vulkan-headers must share the same SDK version.
local vulkan_sdk = "1.4.335+0"
add_requires("vulkan-headers " .. vulkan_sdk)
add_requires("volk " .. vulkan_sdk)
add_requires("vulkan-memory-allocator v3.4.0")
-- VMA depends on unversioned vulkan-headers. Pin it to the same SDK.
add_requireconfs("**.vulkan-headers", { override = true, version = vulkan_sdk })
add_requires("libsdl3 3.4.16")
add_requires("glm 1.0.3")
add_requires("stb 2026.03.18")
add_requires("slang v2026.16")

-- The engine library, then every app built on it.
includes("Engine")
includes("Games/*/xmake.lua")
