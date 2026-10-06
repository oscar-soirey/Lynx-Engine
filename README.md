# Lynx Engine

Lynx is a game engine focused on 2D/pixel art and destructible voxel worlds. It combines a C++ core, an entity-component system, and JavaScript scripts running with QuickJS. The goal is to let users create and test a game directly from the editor, without having to write C++ for common gameplay tasks.

## Features

- **Integrated editor**: viewport, Actor placement, properties panel (*Details*), content browser, and voxel painting.
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

## Technologies

- **C++** — engine core and native components
- **JavaScript / QuickJS** — gameplay scripts
- **EnTT** — ECS registry
- **HRL (Horizon Rendering Library)** — rendering
- **Dear ImGui** — editor interface (modified version)
- **GLFW**, **OpenAL**, **dr_libs**, **cURL**, and **Tracy** — windowing/input, audio, audio decoding, networking, and profiling

## Documentation

The project's documentation site covers, among other things:

- the engine architecture;
- the JavaScript scripting guide and API reference;
- Lynxie's capabilities;
- third-party libraries;
- launcher downloads.

## Author

Developed by **Oscar Soirey**.
