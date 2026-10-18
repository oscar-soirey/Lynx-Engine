# Lynx Engine

Lynx is a game engine focused on 2D/pixel art and destructible voxel worlds. It combines a C++ core, an entity-component system, and JavaScript scripts running with QuickJS. The goal is to let users create and test a game directly from the editor, without having to write C++ for common gameplay tasks.

## Features

- **Integrated editor**: viewport, Actor placement, properties panel (*Details*), content browser, and voxel painting. Its interface is built with **Raven**, a Vulkan UI library (no Dear ImGui), described in XML files that can be edited while the editor runs.
- **Themes**: several built-in themes (Pixel, Lynx, Glass...) and your own: any `.xml` theme file in `%APPDATA%\Lynx\themes` is applied as soon as it is saved (*Options > Interface > New theme*).
- **Vulkan or OpenGL**: games run with Vulkan by default, OpenGL 3.3 is still available (*Options > Project > Game renderer*, or `--vulkan` / `--opengl`), with an automatic fallback to OpenGL on computers without Vulkan.
- **Actor / Level / ECS system**: organizes game objects with components and manages levels.
- **JavaScript scripting**: scripts attached to Actors, lifecycle functions (`BeginPlay`, `Update`, `EndPlay`), and access to objects, transforms, properties, and input.
- **Input system**: configure actions and axes for keyboard, mouse, and gamepad.
- **Voxel worlds**: configurable voxel types, colors, collisions, emissive effects, and destruction rules, with destruction also possible during gameplay.
- **Animation**: animations, blend spaces, state transitions, frame events, parameters, and triggers.
- **Spatial audio**: audio sources attached to Actors, with volume, pitch, looping, and spatialization.
- **Git integration**: manage branches and remotes, staging, commits, and history from the editor.
- **Game reloading**: replace the game DLL without restarting the editor.
- **Lynxie, the built-in AI assistant**: helps manage files, write scripts, modify scenes and terrain/voxels, and compile the game. The model used can be selected.
- **C++ extension**: add native classes and components when JavaScript is not enough.
- **Launcher**: installs the dependencies (Python, CMake, MinGW, Git, Ollama), the AI models, the engine versions and the plugins. It also accepts an engine you built or modified yourself, or a version already installed elsewhere (*Moteur > Ajouter une version locale...*).

## Technologies

- **C++** — engine core and native components
- **JavaScript / QuickJS** — gameplay scripts
- **EnTT** — ECS registry
- **HRL (Horizon Rendering Library)** — rendering (Vulkan, OpenGL 3.3)
- **Raven** — interface of the editor and of the launcher (Vulkan, XML layouts, themes)
- **GLFW**, **OpenAL**, **dr_libs**, **cURL**, and **Tracy** — windowing/input, audio, audio decoding, networking, and profiling

## Building

Windows, MinGW-w64 and CMake (the launcher installs them), OpenAL through vcpkg (`x64-mingw-static`).
Raven is expected next to the engine (`S:/Horizon/Raven` beside `S:/Horizon/Lynx`), or wherever
`-DRAVEN_DIR=...` points.

- `generate-cmake-project.bat` configures `build/` (MinGW Makefiles + the vcpkg toolchain);
- `build.bat` builds everything and starts the editor;
- `build-release.bat` builds the optimized engine used by *Ship Game*.

Game projects find the engine with `find_package(Lynx)`.

## Documentation

The project's documentation site covers, among other things:

- the engine architecture;
- the JavaScript scripting guide and API reference;
- Lynxie's capabilities;
- third-party libraries;
- launcher downloads.

## Author

Developed by **Oscar Soirey**.

## License

Lynx Engine is released under the [Apache License 2.0](LICENCE.md). You can use it, modify it and ship commercial games made with it, with no royalties.

## Support

Lynx is free. If it helps you, you can [buy me a coffee](https://github.com/sponsors/oscar-soirey) on GitHub Sponsors.
