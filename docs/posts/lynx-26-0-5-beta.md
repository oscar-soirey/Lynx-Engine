**Lynx 26.0.5 Beta** is the biggest update since the first preview. Plugins are here, and the voxels learned some physics.

## C++ plugins

Plugins can now add native features to both the **editor** and the **runtime**, without touching the engine itself. They are distributed from their own GitHub repository and installed from the launcher.

If you want to write one, the guide is here: [How to publish a plugin on the official registry](tutorials.html?tutorial=how-to-publish-a-plugin-on-the-official-registry). To see what exists, open the [Plugins](plugins.html) page.

## Two plugins to start with

- **CineCamera**: a camera plugin for cinematic shots.
- **DialogueQuest**: a plugin for dialogues and quests.

Both are plugins rather than engine features on purpose: they are the first examples of what the new system can do.

## Physical properties for voxels

Voxels can now have **physical properties**: liquids, gases, powders, and more. Water and sand do not behave like stone, and now the world can say so. The voxel types themselves are still described in `voxels.json`.

## State machines for animation

The animation system gets **state machines**.

## Editor themes and global scale

The editor now supports **themes** and a **global scale**, which helps on high-DPI screens or if you simply want a different look.

## World files

The world `.xml` and `.hrlv` files are now **merged**.

## And bugs

A round of bug corrections.

Full changelog: [v26.0.4...v26.0.5](https://github.com/oscar-soirey/Lynx-Engine/compare/v26.0.4...v26.0.5)
