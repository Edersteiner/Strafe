-- xmake zed
--
-- Writes .zed/tasks.json and .zed/debug.json with build, run and debug entries
-- for every target using the strafe.app rule.
task("zed")
    set_category("plugin")
    on_run( function ()
      import("core.base.option")
      import("core.project.config")
      import("core.project.project")

      config.load()
      project.load_targets()

      local xmake = os.programfile()

      local apps = { }
      for name, target in pairs(project.targets()) do
        if target:rule("strafe.app") then
          table.insert(apps, target)
        end
      end
      table.sort(apps, function (a, b) return a:name() < b:name() end)

      local function find_app(name)
        for _, app in ipairs(apps) do
          if app:name() == name then
            return app
          end
        end
        local names = { }
        for _, app in ipairs(apps) do
          table.insert(names, app:name())
        end
        raise("%s is not an app. Apps: %s", name, table.concat(names, ", "))
      end

      local run_name = option.get("run")
      if run_name then
        find_app(run_name)
        os.execv(xmake, { "build", run_name })
        os.execv(xmake, { "run", run_name })
        return
      end

      local debug_name = option.get("prepare-debug")
      if debug_name then
        find_app(debug_name)
        if config.get("mode") ~= "debug" then
          os.execv(xmake, { "f", "-m", "debug" })
        end
        os.execv(xmake, { "build", debug_name })
        return
      end

      local function quote(text)
        return '"' .. text .. '"'
      end
      local function list(items)
        local quoted = { }
        for _, item in ipairs(items) do
          table.insert(quoted, quote(item))
        end
        return "[" .. table.concat(quoted, ", ") .. "]"
      end
      local function task_entry(label, args)
        return table.concat({
          "  {",
          '    "label": ' .. quote(label) .. ",",
          '    "command": "xmake",',
          '    "args": ' .. list(args) .. ",",
          '    "cwd": "$ZED_WORKTREE_ROOT",',
          '    "use_new_terminal": false,',
          '    "allow_concurrent_runs": false,',
          '    "reveal": "always"',
          "  }",
        }, "\n")
      end

      local tasks = {
        task_entry("Strafe: build all", { "build", "--all" }),
        task_entry("Strafe: configure debug", { "f", "-m", "debug" }),
        task_entry("Strafe: configure release", { "f", "-m", "release" }),
        task_entry("Strafe: regenerate Zed config", { "zed" }),
      }
      for _, app in ipairs(apps) do
        table.insert(tasks, task_entry(app:name() .. ": build", { "build", app:name() }))
        table.insert(tasks, task_entry(app:name() .. ": run", { "zed", "--run=" .. app:name() }))
      end

      -- Debugging always uses the debug build.
      local debug_entries = { }
      for _, app in ipairs(apps) do
        local dir = path.unix(path.join(config.builddir(), config.plat(), config.arch(), "debug", app:name()))
        local root = "$ZED_WORKTREE_ROOT/"
        table.insert(debug_entries, table.concat({
          "  {",
          '    "label": ' .. quote(app:name()) .. ",",
          '    "adapter": "CodeLLDB",',
          '    "request": "launch",',
          '    "program": ' .. quote(root .. dir .. "/" .. app:filename()) .. ",",
          '    "cwd": ' .. quote(root .. dir) .. ",",
          '    "build": {',
          '      "command": "xmake",',
          '      "args": ' .. list({ "zed", "--prepare-debug=" .. app:name() }) .. ",",
          '      "cwd": "$ZED_WORKTREE_ROOT"',
          "    }",
          "  }",
        }, "\n"))
      end

      local function write(file, entries)
        local text = "[\n" .. table.concat(entries, ",\n") .. "\n]\n"
        local file_path = path.join(os.projectdir(), ".zed", file)
        if os.isfile(file_path) and io.readfile(file_path) == text then
          return
        end
        os.mkdir(path.directory(file_path))
        io.writefile(file_path, text)
      end
      write("tasks.json", tasks)
      write("debug.json", debug_entries)

      cprint("${green}[+]${clear} Wrote Zed tasks and debug configs for %d app(s)", #apps)
    end)
    set_menu {
      usage = "xmake zed [options]",
      description = "Write .zed/tasks.json and .zed/debug.json for every Strafe app.",
      options = {
        { nil, "run", "kv", nil, "Build and run the named app." },
        { nil, "prepare-debug", "kv", nil, "Configure debug mode if needed and build the named app." },
      },
    }
