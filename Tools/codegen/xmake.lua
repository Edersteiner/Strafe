---@diagnostic disable: param-type-not-match, redundant-parameter, need-check-nil, undefined-field

-- Runs the codegen tool over a target's headers before anything compiles:
--
--   target("Strafe")
--       add_rules("strafe.codegen")
--
-- Every app gets it through the strafe.app rule. Generated headers go to
-- build/.gen/<target>/include/, which is added as a public include directory,
-- and each annotated header includes its companion at the end, for example
-- strafe/scene/scene.h ends with #include "strafe/scene/scene.gen.h".
rule("strafe.codegen")
    on_load( function (target)
      target:add("includedirs", path.join(os.projectdir(), "build", ".gen", target:name(), "include"), { public = true })
    end)

    -- Codegen runs at configure time rather than before_build, because xmake
    -- compiles a target's files while its dependencies are still building,
    -- and an app's sources include the engine's generated headers.
    on_config( function (target)
      import("run", { rootdir = path.join(os.projectdir(), "Tools", "codegen", "xmake") })
      run(target)
    end)
