# Strafe

A C++ Game Engine, built on Vulkan 1.4.

Made for my game engine specialization course. Currently a small renderer with
Slang shaders, materials, bindless textures and an obj loader.

## Requirements

- [xmake](https://xmake.io)
- A C++20 compiler (Clang, GCC or MSVC)
- A GPU and driver with Vulkan 1.4 support
- Optional: the Vulkan validation layers, for validation in debug builds

Everything else (SDL3, volk, VMA, Slang, glm, stb) is fetched and built by xmake
the first time you build, so that takes a while.

Tested on Linux and Windows.

## Layout

- `Engine/` is the engine, built as a static library.
- `Games/<Name>/` holds one app each, linked against the engine. Create a new one
  with `xmake new-app <Name>`.

## Building

```sh
xmake f -m debug    # or -m release
xmake
xmake run Week3     # or any other app in Games/
```

Debug builds turn on the validation layers if they're installed. Release builds
skip them.

Shaders are compiled at startup. The engine's shaders come from `Engine/shaders/`
and an app's own from its `shaders/` folder, both copied next to the app's
binary on every build along with its `assets/`.

## For Teachers!

Each weeks assesment is pushed with a tag, so the releases page will let you download a specific version of the repo where an assessment is completed.

Going forward, I may not keep some parts that were developed for assesments, so the current code will not always reflect the work i've done for each weeks assesment.

## Development

- `compile_commands.json` is regenerated on every build, so clangd works out of
  the box.
- Formatting and linting follow `.clang-format` and `.clang-tidy`.
- `xmake zed` writes Zed tasks (build and run) and debugger configs for every
  app. `xmake new-app` runs it for you, so new apps show up in Zed on their own.
  Run it once after cloning, since the files hold this machine's build paths.
