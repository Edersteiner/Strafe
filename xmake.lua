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

target("Strafe")
    set_languages("c++20")
    set_kind("binary")
    add_files("src/**.cpp")
    add_includedirs("include/", { public = true })
    add_packages("vulkan-headers", "volk", "vulkan-memory-allocator", "libsdl3", "glm")

    -- volk loads the Vulkan loader at runtime, so nothing links against it.
    add_defines("VK_NO_PROTOTYPES")
    -- Vulkan clip space uses a [0, 1] depth range.
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE")
