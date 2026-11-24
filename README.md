# Strafe

A C++ Game Engine, built on Vulkan 1.4.

Made for my game engine specialization course. So far it has:

- A renderer with Slang shaders, materials, bindless textures, an obj loader,
  primitives and splitscreen views
- Named input actions over keyboard, mouse and gamepads, for up to four local
  players
- A simple scene format with brushes, objects and markers
- Dear ImGui for debug UI
- Reflection through `[[reflect]]` attributes and a codegen step, with a runtime
  type registry

## Requirements

- [xmake](https://xmake.io)
- A C++20 compiler (Clang, GCC or MSVC)
- A GPU and driver with Vulkan 1.4 support
- Python 3.10 or newer on the PATH, for the codegen tool
- Optional: the Vulkan validation layers, for validation in debug builds

Everything else (SDL3, volk, VMA, Slang, glm, stb, ImGui, tinyobjloader) is
fetched and built by xmake the first time you build, so that takes a while. The
first build also sets up a Python environment in `build/.codegen/` with the
codegen's pinned packages, so it needs internet access.

Tested on Linux and Windows.

## Layout

- `Engine/` is the engine, built as a static library.
- `Games/<Name>/` holds one app each, linked against the engine. Create a new one
  with `xmake new-app <Name>`.
- `Tools/codegen/` is the Python tool that generates reflection code at build
  time.

## Building

```sh
xmake f -m debug    # or -m release
xmake
xmake run Week4     # or any other app in Games/
```

Debug builds turn on the validation layers if they're installed. Release builds
skip them.

Shaders are compiled at startup. The engine's shaders come from `Engine/shaders/`
and an app's own from its `shaders/` folder, both copied next to the app's
binary on every build along with its `assets/`.

## For Teachers!

Each week's assessment is pushed with a tag, so the releases page lets you
download the version of the repo where that assessment is completed.

Going forward, I may not keep some parts that were developed for assessments,
so the current code will not always reflect the work I've done for each week's
assessment.

## Development

- `compile_commands.json` is regenerated on every build, so clangd works out of
  the box.
- Due to codegen, a build is required before the LSP works correctly.
- Formatting and linting follow `.clang-format` and `.clang-tidy`.
- `xmake zed` writes Zed tasks (build and run) and debugger configs for every
  app. `xmake new-app` runs it for you, so new apps show up in Zed on their own.
  Run it once after cloning, since the files hold this machine's build paths.
- The codegen's Python follows `Tools/codegen/ruff.toml`. `pyrightconfig.json`
  points basedpyright at the codegen's environment, so build once before
  opening the Python files.
