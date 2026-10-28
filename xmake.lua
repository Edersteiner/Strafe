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
add_requires("shaderc v2024.1")
-- shaderc depends on unversioned glslang and SPIR-V packages. Pin them to the same SDK.
add_requireconfs("shaderc.glslang", "shaderc.spirv-tools", "shaderc.spirv-headers",
    { override = true, version = vulkan_sdk })
add_requires("libsdl3 3.4.16")
add_requires("glm 1.0.3")

-- Copies runtime files like shaders and assets to the build location
task("stage_runtime_files")
  on_run(function (targetDir)
    os.cp("$(projectdir)/assets", targetDir)
    os.cp("$(projectdir)/shaders", targetDir)
    cprint("${green}[+]${clear} Staged runtime files")
  end)

target("Strafe")
    set_languages("c++20")
    set_kind("binary")
    add_files("src/**.cpp")
    add_includedirs("include/", { public = true })
    add_packages("vulkan-headers", "volk", "vulkan-memory-allocator", "shaderc", "libsdl3", "glm")

    -- volk loads the Vulkan loader at runtime, so nothing links against it.
    add_defines("VK_NO_PROTOTYPES")
    -- Vulkan clip space uses a [0, 1] depth range.
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE")

    if is_mode("debug") then
      add_defines("STRAFE_DEBUG")
    end

    after_build(function (target)
      import("core.project.task")
      task.run("stage_runtime_files", {}, target:targetdir())
    end)
