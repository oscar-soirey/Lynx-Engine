# Lynx Python API reference for local script generation

This is the public Python API shipped with `lynx_editor`. Use these exact
names. Do not invent functions, variables, actor IDs, or engine methods.

## Connect to the open editor

```python
import lynx_editor as lynx
```

Scripts launched by the editor are connected to that editor automatically.

## Find actors in the current level

```python
actors = lynx.actors()                  # list of Actor objects
actors = lynx.actors(cls="Player")      # filter by actual actor class
actors = lynx.actors(id="Player_*")     # filter by actor ID pattern
actor = lynx.actor("Player")            # exact ID; raises LynxError if absent
state = lynx.info()                     # includes selected_actor and actor_count
```

Each Actor has `id`, `cls`, `location`, `rotation`, and `scale` properties.
`location` is a 3D tuple in world units. The actor list returns actual IDs and
classes in the currently open scene. Never assume a player actor exists or has
a particular ID/class; inspect the list and fail safely or ask which actor the
user means when there are zero or multiple plausible matches.

## Move an actor

```python
actor.move(dx, dy, dz)                  # relative delta in world units
actor.move_to(x, y, z)                  # absolute world position
actor.set_transform(location=(x, y, z), relative=True)
actor.refresh()                         # reload actor data from the editor
```

Actor movement uses world units. Voxel positions use integer cell coordinates;
do not assume one world unit equals one voxel. Convert positions with:

```python
cx, cy = lynx.voxels.world_to_cell(x, y)
wx, wy = lynx.voxels.cell_to_world(cx, cy)
```

For a requested delta of N voxel cells along the voxel X axis, convert the
current cell and the target cell back to world coordinates, then pass their
difference to `actor.move`. “Right” relative to a camera is ambiguous unless
the user or documented API specifies the axis; ask instead of guessing.

## Read and write the project files

```python
text = lynx.project_files.read("src/Player.cpp")       # paths from the project root
text = lynx.assets.read("classes/Wanderer.js")          # paths inside assets/
data = lynx.assets.read_json("voxels.json")
names = lynx.assets.list("classes", pattern="*.js", recursive=True)
lynx.compile()                                          # build the game DLL after a C++ change
info = lynx.actor("Pawn").details()                     # properties and functions of an actor
```

To look something up before answering (a file that is not in the context, a
value), write a script whose FIRST line is `# lynxie: read` and that prints
what you need : its output is sent back to you, then you answer the user.

To CHANGE or CREATE a file, do not use Python (`write`, `.replace`) : answer
with SEARCH / REPLACE blocks, applied and checked by the editor (see the rules
"EDITING FILES" of your instructions).

## Script behavior

- Use the editor's documented `lynx_editor` API and Python standard library.
- Only use names listed here or in the live command reference supplied to the
  model. If the docs do not establish a function, do not make one up.
- For a scene operation, inspect `lynx.actors()` first. Match the user's target
  against actual IDs/classes. If the match is not unique, print the candidates
  and stop without changing the scene.
- For an action, give a brief explanation in the language the user wrote in
  (English when unclear), then put the complete executable Python in exactly
  one `python` fenced code block. It runs as soon as the answer arrives.
- A question gets a text answer with no `python` block (examples in other
  languages go in ```cpp / ```js blocks : they are never run).
- Do not output JavaScript, pseudocode, hypothetical functions, or made-up
  placeholder state.

## Check the JavaScript API of the engine

Before writing a `.js` file, check that the engine functions exist (the editor
refuses an edit that calls one that does not):

```python
# lynxie: read
import lynx_editor as lynx
for e in lynx.js_api("Light2D"):          # object, component or function name
    print(e["owner"], e["signature"], "-", e["doc"])
```

