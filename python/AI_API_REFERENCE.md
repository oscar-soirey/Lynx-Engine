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

## Script behavior

- Use the editor's documented `lynx_editor` API and Python standard library.
- Only use names listed here or in the live command reference supplied to the
  model. If the docs do not establish a function, do not make one up.
- For a scene operation, inspect `lynx.actors()` first. Match the user's target
  against actual IDs/classes. If the match is not unique, print the candidates
  and stop without changing the scene.
- Give a brief French explanation, then put the complete executable Python in
  exactly one `python` fenced code block.
- Do not output JavaScript, pseudocode, hypothetical functions, or made-up
  placeholder state.
