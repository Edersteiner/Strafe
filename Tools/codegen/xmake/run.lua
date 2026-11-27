---@diagnostic disable: param-type-not-match, redundant-parameter, need-check-nil, undefined-field

-- The strafe.codegen rule's work: a Python environment, the header list, and
-- the tool itself when anything it reads has changed.

import("core.project.depend")
import("lib.detect.find_tool")

local function tool_dir()
  return path.join(os.projectdir(), "Tools", "codegen")
end

local function state_dir()
  return path.join(os.projectdir(), "build", ".codegen")
end

-- A private virtual environment with the pinned requirements, so every machine
-- runs the same tree-sitter whatever its system Python has installed.
local function ensure_python()
  local venv = path.join(state_dir(), "venv")
  local python = is_host("windows") and path.join(venv, "Scripts", "python.exe") or path.join(venv, "bin", "python")
  local requirements = path.join(tool_dir(), "requirements.txt")
  local stamp = path.join(venv, "strafe-requirements.txt")
  local wanted = io.readfile(requirements)
  if os.isfile(python) and os.isfile(stamp) and io.readfile(stamp) == wanted then
    return python
  end

  local system_python = find_tool("python3") or find_tool("python")
  if not system_python then
    raise("strafe.codegen needs Python 3.10 or newer on the PATH")
  end
  cprint("${green}[+]${clear} Setting up the codegen Python environment in %s", venv)
  os.vrunv(system_python.program, { "-m", "venv", venv })
  os.vrunv(python, { "-m", "pip", "install", "--disable-pip-version-check", "--quiet", "-r", requirements })
  io.writefile(stamp, wanted)
  return python
end

-- Include directories inside the target's own folder, plus src/ for apps,
-- which include their headers by relative path.
local function include_roots(target)
  local roots = { }
  local seen = { }
  local function add(dir)
    if not seen[dir] and os.isdir(dir) and dir:startswith(target:scriptdir()) then
      seen[dir] = true
      table.insert(roots, dir)
    end
  end
  -- xmake keeps include directories relative to the project root.
  for _, dir in ipairs(table.wrap(target:get("includedirs"))) do
    add(path.absolute(dir, os.projectdir()))
  end
  add(path.join(target:scriptdir(), "src"))
  table.sort(roots)
  return roots
end

function main(target)
  local out = path.join(os.projectdir(), "build", ".gen", target:name())
  -- The engine registers its types and commands first, then the app, so each
  -- kind of target defines its own functions.
  local register_prefix = target:rule("strafe.app") and "registerApp" or "registerEngine"
  local roots = include_roots(target)
  local headers = { }
  for _, root in ipairs(roots) do
    for _, pattern in ipairs({ "**.h", "**.hpp" }) do
      for _, file in ipairs(os.files(path.join(root, pattern))) do
        table.insert(headers, path.absolute(file))
      end
    end
  end
  headers = table.unique(headers)
  table.sort(headers)

  local tool_files = os.files(path.join(tool_dir(), "strafe_codegen", "**.py"))
  table.insert(tool_files, path.join(tool_dir(), "requirements.txt"))
  table.sort(tool_files)

  -- Content hashes rather than modification times, which xmake compares to the
  -- second, so an edit right after a build can go unnoticed.
  local fingerprint = {}
  for _, file in ipairs(table.join(headers, tool_files)) do
    table.insert(fingerprint, file .. "=" .. hash.xxhash128(file))
  end
  -- Reruns once a checkout gains Zed settings, so they get the file types.
  local zed_dir = path.join(os.projectdir(), ".zed")
  table.insert(fingerprint, "zed=" .. tostring(os.isdir(zed_dir)))

  -- Attributes are standard C++ the compilers don't recognize. GCC can only
  -- silence unscoped attributes all at once.
  if target:has_tool("cxx", "cl", "clang_cl") then
    target:add("cxxflags", "/wd5030")
  else
    target:add("cxxflags", "-Wno-attributes")
  end

  depend.on_changed( function ()
    local python = ensure_python()
    os.mkdir(state_dir())
    local inputs = path.join(state_dir(), target:name() .. ".inputs")
    io.writefile(inputs, table.concat(headers, "\n") .. "\n")

    local arguments = { "-m", "strafe_codegen", "--project", os.projectdir(), "--target", target:name(), "--out", out, "--inputs", inputs, "--register-prefix", register_prefix }
    for _, root in ipairs(roots) do
      table.insert(arguments, "--root")
      table.insert(arguments, root)
    end
    -- Only in a checkout set up for Zed, which has its project settings folder.
    if os.isdir(zed_dir) then
      table.insert(arguments, "--zed-settings")
      table.insert(arguments, path.join(zed_dir, "settings.json"))
    end
    os.execv(python, arguments, { envs = { PYTHONPATH = tool_dir(), PYTHONDONTWRITEBYTECODE = "1" } })
  end, {
    dependfile = path.join(state_dir(), target:name() .. ".d"),
    files = tool_files,
    values = fingerprint,
    changed = not os.isfile(path.join(out, "src", "register.gen.cpp")),
  })

  target:add("files", path.join(out, "src", "register.gen.cpp"))
end
