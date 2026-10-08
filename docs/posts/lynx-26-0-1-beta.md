It's out. Lynx **26.0.1 Beta** is on GitHub, and it's the very first public preview of the engine. 🎉

I've been building Lynx on my own for a while now, and shipping something that other people can actually download feels a bit unreal. So, hi, and welcome to the devlog!

## What is Lynx, again?

Lynx is a C++ game engine for 2D and pixel-art games, with **destructible voxel worlds**, an integrated editor and **JavaScript scripting**. The core is C++, but you never *have* to write C++: gameplay lives in JavaScript, and levels, voxels and Actors are built from the editor. C++ stays optional, for the day you want your own components.

## About this release

It is tagged as a **pre-release**, and I mean it: this is a first look, not a polished 1.0. Things will move, and some edges will be rough. I would rather get it in your hands early and shape it with real feedback than polish it alone in a cave.

## What I'm working on right now

The big thing on my plate is **AI model integration**. The idea is **Lynxie**, an assistant that lives inside the engine and can use the engine itself as a tool: files, scripts, the scene, terrain, even compiling the game. You can read more on the [Lynxie page](lynxie.html).

I'm also working on a model specialized for Lynx, but that one is a longer road, so no promises on timing.

## Your turn

If you try the beta and something breaks (it might!), or you have an idea, open an issue on [GitHub](https://github.com/oscar-soirey/Lynx-Engine/issues). As a solo dev, every report genuinely helps me pick what to work on next.

Thanks for being here at the start. More soon!
