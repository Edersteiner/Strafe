-- The engine, linked statically into every app.
target("Strafe")
    set_kind("static")
    set_languages("c++20", { public = true })
    add_files("src/**.cpp")
    add_includedirs("include", { public = true })

    -- Public because engine headers include them, so apps need them too.
    add_packages("vulkan-headers", "volk", "vulkan-memory-allocator", "libsdl3",
                 "glm", { public = true })
    -- Only used inside the engine's own source files.
    add_packages("slang", "stb")

    -- Public, so apps see the same Vulkan declarations and glm conventions as
    -- the engine. An app built without GLM_FORCE_DEPTH_ZERO_TO_ONE would make
    -- projection matrices for the wrong depth range.
    -- volk loads the Vulkan loader at runtime, so nothing links against it.
    add_defines("VK_NO_PROTOTYPES", { public = true })
    -- Vulkan clip space uses a [0, 1] depth range.
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE", { public = true })

    if is_mode("debug") then
      add_defines("STRAFE_DEBUG", { public = true })
    end

-- Turns a target into a Strafe app:
--
--   target("Week4")
--       add_rules("strafe.app")
--       add_files("src/**.cpp")
--
-- It builds a binary linked against the engine into its own folder,
-- build/<plat>/<arch>/<mode>/<name>/, and stages the runtime files next to it:
--   engine/shaders/  the engine's Slang library and materials
--   shaders/         the app's own shaders, searched first
--   assets/          the app's assets
-- The app's shaders/ and assets/ sit next to its xmake.lua and are optional.
rule("strafe.app")
    on_load( function (target)
      target:set("kind", "binary")
      target:add("deps", "Strafe")
      target:set("targetdir", path.join(target:targetdir(), target:name()))
    end)

    after_build( function (target)
      local outdir = target:targetdir()
      local staged = {
        { path.join(os.projectdir(), "Engine", "shaders"), path.join(outdir, "engine", "shaders") },
        { path.join(target:scriptdir(), "shaders"), path.join(outdir, "shaders") },
        { path.join(target:scriptdir(), "assets"), path.join(outdir, "assets") },
      }
      for _, entry in ipairs(staged) do
        local src, dst = entry[1], entry[2]
        -- Cleared first, so files deleted from the source don't linger.
        os.tryrm(dst)
        if os.isdir(src) then
          os.mkdir(path.directory(dst))
          os.cp(src, dst)
        end
      end
      cprint("${green}[+]${clear} Staged runtime files for %s", target:name())
    end)

-- xmake new-app <Name>
--
-- Copies the app template and replaces AppTemplateName and app_template_name
-- with the given name in the command.
task("new-app")
    set_category("plugin")
    on_run( function ()
      import("core.base.option")

      local name = option.get("name")
      if not name or not name:match("^[%a_][%w_]*$") then
        raise("The name must be a C++ identifier, e.g. xmake new-app Week4")
      end
      if name == "Strafe" then
        raise("Strafe is the engine's target name")
      end

      local template = path.join(os.projectdir(), "Engine", "template")
      local destination = path.join(os.projectdir(), "Games", name)
      if os.exists(destination) then
        raise("%s already exists", destination)
      end

      local file_name = name:lower()
      for _, source in ipairs(os.files(path.join(template, "**"))) do
        local relative = path.relative(source, template):gsub("app_template_file", file_name)
        local text = io.readfile(source)
        text = text:gsub("AppTemplateName", name):gsub("app_template_file", file_name)
        local target_file = path.join(destination, relative)
        os.mkdir(path.directory(target_file))
        io.writefile(target_file, text)
      end

      cprint("${green}[+]${clear} Created Games/%s", name)
      cprint("    Build and run it with: ${bright}xmake build %s && xmake run %s", name, name)
    end)
    set_menu {
      usage = "xmake new-app <Name>",
      description = "Create a new Strafe app in Games/<Name> from Engine/template.",
      options = {
        { nil, "name", "v", nil, "The app's name, a C++ identifier such as Week4." },
      },
    }
