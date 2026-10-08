**Lynx 26.0.3 Beta** is about the things that make a project feel like *your game*: its UI, its icon, its first screen and the way the player is controlled.

## A new widget editor

Lynx has a UMG-like widget system: a `UserWidget` is an asset-backed tree of panels and controls, and it works from both C++ and JavaScript. 26.0.3 adds a **new widget editor** to build those trees in the editor and save them as `.widget` assets.

Layouts use slate units with a DPI reference, so an interface designed for 1920×1080 can scale to other viewport sizes.

## Ship your game with your own icon

When you ship a game, you can now give it a **custom icon**, so the executable looks like your game and not like an engine sample.

## A player controller, Unreal-style

26.0.3 introduces an **Unreal-like player controller**. If you have used Unreal Engine before, the idea of a controller that possesses a pawn should feel familiar. I will write about how it fits in the engine once it settles.

## Splash screen

Games can now show a **splash screen** at startup.

## And the usual

Bug corrections, as always.

Full changelog: [v26.0.2...v26.0.3](https://github.com/oscar-soirey/Lynx-Engine/compare/v26.0.2...v26.0.3)
