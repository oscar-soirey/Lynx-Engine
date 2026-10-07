# Lynx Engine Documentation Site

A simple static documentation website for Lynx Engine, a game engine for small games and massive open worlds with dynamic voxel destruction.

## Pages
- `index.html` — engine overview
- `architecture.html` — C++ / ECS / systems architecture
- `scripting.html` — complete JavaScript scripting guide
- `api.html` — compact JavaScript API reference
- `thirdparty.html` — third-party libraries (HRL, EnTT, QuickJS, GLFW, Dear ImGui; drop real logos in assets/libs/<id>.png)
- `plugins.html` + `plugins.js` — plugin registry browser (reads registry/plugins.json from the main repo) and publish form
- `devlog.html`, `tutorials.html` + `collection.js` + `md.js` — public content collections (read `<dir>/index.json` and `<dir>/<slug>.md`; devlog uses `posts/`, tutorials use `tutorials/`). A collection is configured by `window.LYNX_COLLECTION` in the page
- `devlog-admin.html`, `tutorials-admin.html` + `collection-admin.js` — local editors: preview, image/GIF/YouTube support, exports a zip to unzip at the site root (tutorials also have a level field)
- to add another collection, copy a pair of pages and change the config
- `site.js` — pipeline highlight, toolbar, hero jump
- `assets/` — pixel art sprites (cubes, hero sheet, mushroom, pixelated logo)
- `styles.css` — pixel-art UI and responsive layout

## Visual style
The palette is based on the Lynx editor screenshot: black UI chrome, warm beige panels, deep purple viewport colors, and green action accents. The site uses `Press Start 2P` and `VT323` from Google Fonts.

## Usage
Open `index.html` in a browser.

## Source of truth
The JavaScript documentation was built from the engine source, including `src/scripting/ScriptSystem.cpp`, `src/gameplay/Actor.*`, `src/gameplay/Components.h`, `src/gameplay/Component.h`, `src/core/Level.h`, `src/core/Engine.h`, and related systems present in the project.

## Publishing on GitHub Pages
Run `python bump-assets.py` after adding or editing a post or tutorial, and before each commit. It rebuilds `posts/data.js` and `tutorials/data.js` (so the devlog and tutorials also work when a page is opened directly from disk, without a web server) and it adds `?v=<hash>` to the CSS/JS links of every page so browsers never keep serving an outdated `styles.css` or script (a stale stylesheet breaks the toolbar and the devlog cards). `.nojekyll` disables Jekyll processing.
