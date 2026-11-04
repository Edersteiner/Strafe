# Strafe
A C++ Game Engine, built on Vulkan 1.4.

Made for my game engine specialization course. Right now it renders a triangle,
while I work through a refactor from one big hardcoded file into a proper
renderer.

## Requirements
- [xmake](https://xmake.io)
- A C++20 compiler (Clang, GCC or MSVC)
- A GPU and driver with Vulkan 1.4 support
- Optional: the Vulkan validation layers, for validation in debug builds

Everything else (SDL3, volk, VMA, shaderc, glm) is fetched and built by xmake
the first time you build, so that takes a while.

Tested on Linux and Windows.

## Building
```sh
xmake f -m debug    # or -m release
xmake
xmake run Strafe
```

Debug builds turn on the validation layers if they're installed. Release builds
skip them.

Shaders are compiled at startup from `shaders/`, which gets copied next to the
binary on every build.

## For Teachers!
Each weeks assesment is pushed with a tag, so the releases page will let you download a specific version of the repo where an assessment is completed.

Going forward, I may not keep some parts that were developed for assesments, so the current code will not always reflect the work i've done for each weeks assesment.

## Development
- `compile_commands.json` is regenerated on every build, so clangd works out of
  the box.
- Formatting and linting follow `.clang-format` and `.clang-tidy`.
