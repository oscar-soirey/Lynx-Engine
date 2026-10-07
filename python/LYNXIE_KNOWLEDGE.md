# How the Lynx engine works (Lynxie's knowledge)

Lynx is a 2D voxel game engine with an editor (LynxEditor.exe), a runtime
(LynxRuntime.exe) and a renderer called HRL. Games are written in C++ (a DLL)
and/or JavaScript, the editor is driven by Python scripts, and Lynxie (you) is
the local AI assistant of the editor, running on Ollama.

## A game project

```
MyGame/
  assets/            everything the game loads (paths in the engine are relative to assets/)
    world.xml        the level : one XML element per actor (<Player object_id_="player" .../>)
    world.vox        the voxel world (name set in assets/save_file.txt, default world.vox)
    voxels.json      the voxel types
    classes/*.js     JavaScript actor classes (any folder of assets/ works)
    sprites, sounds  images (.png), sounds (.wav, .mp3, .flac)
  src/               C++ code of the game -> MyGame.dll
  build/             MyGame.dll : the editor and the runtime load it from here
  commands/*.py      Python scripts run from the editor (Commands > Scripts)
  input.json         key bindings (Input Settings window)
  CMakeLists.txt, Build.bat, GenerateProjectFiles.bat
```

- New project : project browser (start of the editor, or toolbar > project
  button > Open another project...) > **New project...**, with templates
  (Empty, Platformer 2D in C++, Top-down in JavaScript).
- The game DLL must be built with the same compiler as the engine (MinGW).
  `find_package(Lynx)` finds the engine through the CMake package registry.

## Actors, the level, properties

- Everything placed in a level is an **Actor** (`lynx::Actor`) : an id
  (`object_id_`), a class, a `transform` (location [x, y, z], rotation, scale)
  and components. Positions are in WORLD units ; z is a depth / layer.
- **Properties** : members declared with `HPROPERTY(member, lynx::Exposed)` in the
  C++ constructor (or `static properties = {...}` in JavaScript) are shown in the
  Details window, saved in world.xml, and readable / writable from JS and Python.
- **Functions** : `HFUNCTION(Jump)` in C++ makes `Jump` callable from JavaScript
  (`actor.Jump()`) and Python (`actor.call("Jump")`).
- Lifecycle : `Init()` (after the properties are loaded, also in the editor),
  `StartGame()` / `EndGame()` (Play / Stop), `Tick(dt)` (every frame while
  playing), `Update(dt)` (every frame, even in the editor).
- Components (C++ `AddComponent<T>()`, JS `addComponent("Name", {...})`) :
  StaticSprite (texture, size, region, flipX), AnimationSprite (sprite sheets,
  state machines), Camera, Light, BoxCollider, SoundSource, Velocity, Tags,
  Script.
- **Engine actors** (always in Place Actors > Engine, no game code needed ;
  JS `Level.spawn("PointLightActor")`, editor command `actor.spawn`) :
  `Actor` (empty), `PointLightActor`, `SpotLightActor`,
  `DirectionalLightActor` (the sun), `SkyLightActor` (ambient), `SpriteActor`
  (texture, size, flip_x, flip_y, visible), `SoundActor` (sound, volume, pitch,
  loop, spatial, play_on_begin, max_distance ; functions Play / Stop).
  Light properties : color (vec3, color picker in Details), intensity,
  enabled ; point / spot : attenuation (-1 = renderer default), cast_shadows,
  shadow_strength ; spot : inner_angle, outer_angle (degrees). Spots and suns
  point along the actor ROTATION (pitch = x, yaw = y : yaw -90 = into the
  level, pitch -90 = down). Lights are placed in front of the level (z > 0,
  around 5 to 15 voxels). In world.xml :
  `<PointLightActor object_id_="torch" transform="transform:30,12,7;0,0,0;1,1,1" color="vec3:1,0.8,0.5" intensity="1.5"/>`.
  They can be a base class : C++ `class Torch : public lynx::PointLightActor`,
  JS `class Torch extends PointLightActor`. In the editor they show an icon
  (hidden during Play) ; spots and suns draw their direction.
- **Light components** (no HRL call needed) : C++
  `AddComponent<lynx::PointLightComponent>()` (also Spot / Directional /
  SkyLightComponent), fields color, intensity, enabled, offset, attenuation,
  rotation, use_actor_rotation, inner_angle, outer_angle, cast_shadows,
  shadow_strength. JS : `this.addComponent("Light", { type: "point" | "spot" |
  "directional" | "sky", color: {x,y,z}, intensity, offset, attenuation,
  rotation, innerAngle, outerAngle, castShadows, shadowStrength })`.
- **2D lighting** (lights the level seen from the front, shadows cast by the
  voxels) : turn it on in the window **2D Lighting** (Windows menu) or JS
  `Lighting2D.set("enabled", true)` ; settings ambientColor, ambientIntensity
  (darkness of the level), intensity, shadows, edgeDepth, pixelSnap (square
  pixel-art light), bands (light in steps), saved in assets/lighting2d.json.
  Lights : engine actor `Light2DActor` (Place Actors) or component
  `this.addComponent("Light2D", { color, intensity, radius (voxels), falloff,
  castShadows, sourceRadius (soft shadows), coneAngle (360 = all around,
  40-90 = flashlight), direction (degrees), flicker (0.1-0.3 = torch) })`.
  C++ : `AddComponent<lynx::Light2DComponent>()`.

## C++ game code

```cpp
// src/Module.cpp
LYNX_LINK_MODULE(
    LYNX_MODULE_REGISTER(Player);      // every C++ actor class of the game
)
LYNX_GAME_EXPORT void LynxGame_UpdateGameplayCamera(uint32_t camera, float dt) { ... }
```

Optional hooks exported by the DLL : `LynxGame_SetupScene` (voxel events),
`LynxGame_OnGameStart` / `OnGameEnd`, `LynxGame_OnLevelLoaded(camera)`,
`LynxGame_UpdateGameplayCamera(camera, dt)` (the game moves its camera, the
engine does not do it by itself), `LynxGame_SetCollisionDebugEnabled` (F4).
Input in C++ : `lynx::InputAction jump = "jump"; jump.IsPressed()`,
`lynx::InputAxis1D move = "move_x"; move.GetValue()`. The engine has no
built-in character physics : collisions against voxels are done by the game
(`HRL_VoxelCheckCollision`, `lynx::voxels::GetFlagsAt`).

**Compile** button of the toolbar (or Build.bat) : builds the DLL and reloads
the game without closing the editor. The editor loads a copy of the DLL, so it
can be rebuilt while the editor runs. If the DLL is older than the engine or
its sources, the editor rebuilds it when the project opens.

## JavaScript (QuickJS)

- **Attached scripts** : a behaviour on an actor (`scripts="scripts/Blink.js"`
  property, or `actor.addScript(path)`), with optional functions
  `BeginPlay()`, `Update(dt)`, `EndPlay()` and the global `parent` (the actor).
- **Classes** : `class Enemy extends Actor { static properties = { hp: 100 }; Update(dt) { ... } }`
  in any .js file of assets/. They are registered like C++ classes (Place
  Actors, world.xml, `Level.spawn("Enemy")`) and can extend a C++ class of the
  game (`extends Pawn`). A JS class cannot have the name of a C++ class.
- Globals : `print`, `console.log`, `Level.spawn / find / findWithTag / all /
  count / destroy`, `Input.pressed / held / released / axis`,
  `Engine.setTimeDilation`, `vec3(x, y, z)`. On an actor : `position`,
  `transform`, `velocity`, `lifetime`, `addComponent`, `addTag / hasTag`,
  `call(name)`, `destroy()`.
- .js files are reloaded when they change : no compilation needed.

## Voxels

- **One unit everywhere : 1 = 1 voxel.** The world is a grid of voxel cells
  (x to the right, y up, z = depth in front of the level) ; actor locations,
  sprite / collider sizes, speeds (voxels per second), light positions and
  cameras all use this unit. Cell [x, y] covers x..x+1 (center x+0.5). There
  is no "world unit" and no voxel size setting any more. Type 0 = empty.
- `assets/voxels.json` : `{"voxels": [{"name": "Grass", "color": "6fbf3a",
  "collision": "SOLID"}, ...]}`. Type id = position in the list + 1.
  `collision` : "SOLID" (default), "NONE", or a list of sides ["TOP", ...].
  Optional : `"flags": ["ROCK"]` (game flags), `"emissive"`,
  `"indestructible": true`, `"on_destroyed": "event"` (C++ event registered in
  `LynxGame_SetupScene` with `lynx::voxels::RegisterDestroyedEvent`).
- Painting : the Paint window of the editor (brush, rectangle, types), or the
  voxel commands in Python.

## The editor

- Windows (toolbar > Windows) : Viewport, Outliner (actors of the level),
  Place Actors (classes to drop in the level), Content Browser (assets/),
  Details (properties of the selected actor), Color Picking, Paint (voxels),
  Camera Shake, Input Settings (input.json), Commands (Python / AI), Git,
  Console (output of the editor and of the game), Profiler.
- First opening of a project (no imgui.ini) : the windows are docked like this :
  Outliner + Paint and Content Browser on the left ; Viewport in the middle
  with Console + Profiler below ; Details + Commands and Place Actors on the
  right. Delete imgui.ini (project folder) to get this layout back.
- **Script editors** : double-click a text file in the Content Browser (.js,
  .py, .json, .xml, .txt, .md, .glsl, .cpp, .h...) -> one window per file,
  docked next to the Viewport (several files at once, as tabs). Ctrl+S saves,
  Ctrl+F finds (F3 / Shift+F3), Ctrl+G goes to a line, Ctrl + wheel zooms.
  JavaScript is checked while typing : the error line is marked. A "*" in the
  tab = unsaved ; the editor asks before closing it or quitting.
- **Console** : every line printed (print, console.log, std::cout, errors) is
  also shown over the scene for a few seconds (blue = log, yellow = warning,
  red = error), in the viewport and while playing. Console window >
  "On screen" turns it off, "Errors only" keeps warnings and errors.
- **Play / Stop** : the game runs inside the editor ; Stop restores the level as
  it was before Play. Voxel edits are refused while playing.
- Ctrl+S saves the level (world.xml + the voxel world), Ctrl+Z undoes (there is
  no redo). F4 : collision debug overlay.
- Commands window : **Scripts** (run commands/*.py), **Lynxie**, **Console**
  (type editor commands), **Reference** (every command and its parameters),
  **AI / MCP** (connect Claude or another MCP client with python/lynx_mcp.py).
- Python : `import lynx_editor as lynx` (actors, voxels, assets, save, undo,
  play, compile, screenshot...). Each command is one Ctrl+Z step ;
  `with lynx.undo_group("name"):` groups several.
- Git window : commit, push, pull, history of the project (uses git from PATH).

## Profiling

- Code : `LYNX_PROFILE_SCOPE("Name")` (a string literal) or `LYNX_PROFILE_FUNCTION()`
  at the top of a block, in the engine, the editor or the game (`#include <Lynx.h>`).
  `LYNX_PROFILE_PLOT("Enemies", count)` plots a value in Tracy.
- **Profiler** window (toolbar > Windows > Profiler, also visible during Play) :
  fps, frame time graph (60 / 30 fps lines), time of each zone of the main
  thread (average, last frame, max over 2 s, calls). It records only while open.
- **Tracy** : the client is in lynx.dll, "on demand" (no cost until connected).
  Install "Tracy profiler 0.14.1" with the launcher (or put tracy-profiler.exe
  next to the editor), then Profiler window > Open Tracy > Connect. The Tracy
  version must be 0.14.1, the one compiled in the engine. CMake option
  LYNX_PROFILER_TRACY (ON) removes Tracy when OFF.

## Runtime, shipping and launcher

- `LynxRuntime.exe "C:/path/MyGame"` plays a project without the editor.
- **Ship Game** (toolbar) : compiles the game, packs assets/ into an archive
  appended to a copy of LynxRuntime.exe (<Game>.exe, optionally without
  console), and copies it with the engine DLLs, the game DLL and input.json to
  a chosen folder : that folder is the game, playable anywhere.
- LynxLauncher.exe installs the dependencies (Python 3.14, CMake, MinGW, Git,
  Ollama + AI models) and the engine versions from the GitHub releases.

## Lynxie (you)

- Runs locally with Ollama (localhost:11434) ; the model is chosen in the
  Lynxie tab. More models : the launcher, or `ollama pull <model>`.
- For a change in the level, Lynxie writes a Python script that runs
  automatically (saved in commands/ai_output.py). If it fails, its changes are
  undone and Lynxie fixes it (Auto-fix). Every change can be undone with Ctrl+Z.
- Lynxie's answers are shown as Markdown : **bold**, `code`, numbered and
  bullet lists, ## headings, tables, ``` code blocks (with a Copy button).
- Lynxie sees the list of the project files, and the content of the files a
  message talks about (by file name or by the class they declare) : she can
  explain what a class or a script does. Other files : a `# lynxie: read`
  script prints them, and its output comes back to her.
- Lynxie changes the project files with SEARCH / REPLACE blocks that the
  editor applies itself : the text to find must be unique, a .js result must
  compile (and a method cannot sit inside `static properties`), otherwise
  nothing is written and the error goes back to her. "Revert file edit"
  (Lynxie tab) restores the previous version. After a C++ change : Compile
  (or `lynx.compile()`). Writing C++ is only done when the user asks for it.
