# Lynx Engine Documentation Site

A simple static documentation website for Lynx Engine, a game engine for small games and massive open worlds with dynamic voxel destruction.

## Pages
- `index.html` — engine overview
- `architecture.html` — C++ / ECS / systems architecture
- `scripting.html` — complete JavaScript scripting guide
- `api.html` — compact JavaScript API reference
- `thirdparty.html` — third-party libraries (HRL, EnTT, QuickJS, GLFW, Dear ImGui; drop real logos in assets/libs/<id>.png)
- `site.js` — pipeline highlight and hero jump
- `assets/` — pixel art sprites (cubes, hero sheet, mushroom, pixelated logo)
- `styles.css` — pixel-art UI and responsive layout

## Visual style
The palette is based on the Lynx editor screenshot: black UI chrome, warm beige panels, deep purple viewport colors, and green action accents. The site uses `Press Start 2P` and `VT323` from Google Fonts.

## Usage
Open `index.html` in a browser.

## Source of truth
The JavaScript documentation was built from the engine source, including `src/scripting/ScriptSystem.cpp`, `src/gameplay/Actor.*`, `src/gameplay/Components.h`, `src/gameplay/Component.h`, `src/core/Level.h`, `src/core/Engine.h`, and related systems present in the project.
