# Project foundation

## Scope

Create the initial cross-platform C/CMake game project and main entry point.
Do not commit or push changes.

Superseded by `.context/core-renderer-scaffolding.md` for current service architecture.

## Initial structure and decisions

- `CMakeLists.txt`: CMake 3.24+, strict C17 executable `axiovox`, required Vulkan
  discovery through the imported `Vulkan::Vulkan` target, compiler-specific warnings.
- `src/main.c`: standard console entry point returning `EXIT_SUCCESS`.
- Windows, Linux, and macOS are intended targets. Platform-specific windowing,
  Vulkan instance creation, and the game loop are outside this initial scaffold.
- Build artifacts belong in ignored `build/` or `build-*/` directories.

## Validation

Passed on macOS with AppleClang 21 and Vulkan SDK 1.4.328:
`cmake -S . -B build`, `cmake --build build --config Debug`,
`./build/axiovox`, and `git diff --check`.
Windows and Linux builds have not been run.
