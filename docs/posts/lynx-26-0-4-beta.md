**Lynx 26.0.4 Beta** is the "visual tools" release. Two systems that used to be pure code now have an editor.

## A visual editor for animations

The animation system (`animation`, `blend_space` and `anim_manager`) handles playback, frame events, parameters, triggers and transitions. You can now work on it **visually** in the editor instead of describing everything in code.

## A visual editor for behavior trees

The same goes for **AI**: behavior trees get their own visual editor. A tree is much easier to read as a graph than as a pile of conditions, and that is the point.

## Convert a sprite to voxels

There is a new **"Convert sprite to voxels"** button. It turns a sprite into voxels, which is handy when your art starts as a normal 2D image and you want it to live in a destructible voxel world. The [documentation](https://oscar-soirey.github.io/Lynx-Engine/) explains how to use it.

## Stability

- fixed several bugs that could **crash the engine**
- corrected some engine **templates**

Full changelog: [v26.0.3...v26.0.4](https://github.com/oscar-soirey/Lynx-Engine/compare/v26.0.3...v26.0.4)
