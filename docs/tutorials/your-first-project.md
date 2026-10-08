Welcome to Lynx. In this tutorial you will open a project, put an Actor in the world, give it a script and press Play. It takes about ten minutes and you do not need to know C++.

## What you will learn

- what a Lynx project looks like on disk
- what the main editor panels are for
- how to place an Actor and attach a script to it
- how to run your level in Play mode

## Step 1: understand the project folder

A Lynx project is a normal folder. The editor manages its content for you, but it helps to know where things live:

```text
MyGame/
  assets/
    classes/
    scripts/
    ui/
    voxels.json
    input.json
  levels/
  CMakeLists.txt
  src/
  build/
```

- `assets/` holds everything the game loads: scripts, UI, images, sounds, `voxels.json` and `input.json`.
- `levels/` holds your levels.
- `CMakeLists.txt`, `src/` and `build/` are only used if you extend the engine with C++. For now you can ignore them.

> `assets/classes/` and `assets/scripts/` are conventions. A JavaScript file can live anywhere under `assets/`.

## Step 2: open the project

Launch the editor and open your project folder. The editor is built around dockable panels. These are the ones you will use right now:

- **Viewport**: the world. You edit and play here.
- **Outliner**: the list of Actors in the level, organized in folders.
- **Details**: the properties of the selected Actor.
- **Content Browser**: the assets of your project.

## Step 3: place an Actor

An Actor is any object that exists in the world. It has a transform (position, rotation, scale), can carry scripts and can own components.

1. Add an Actor to the level with **Place Actors**.
2. Select it in the Viewport or in the Outliner.
3. Look at the **Details** panel: you can now edit its reflected properties, such as its position.

Give it an id you will recognize, for example `Hero`. Scripts and other Actors will use that id to find it.

## Step 4: write a script

Create a file named `Hero.js` in `assets/scripts/` (with any text editor, or from the Content Browser). Paste this:

```js
function BeginPlay() {
  console.log("Hello from", parent.id);
}

function Update(dt) {
  parent.position.x += 2 * dt;
}
```

A script is **not** a class. Lynx calls `BeginPlay()` once when the game starts and `Update(dt)` on every tick. `parent` is the Actor the script is attached to, and `dt` is the time since the last frame in seconds.

## Step 5: attach the script

Select your Actor and look for its **scripts** property in **Details**. Add the path of your file:

```text
scripts/Hero.js
```

Several scripts can be attached to the same Actor, and the same script can be attached to many Actors. Each attachment keeps its own state.

## Step 6: press Play

Press **Play** in the editor. The game module is loaded and your level starts running. Your Actor should slide to the right, and the console prints your message.

Stop the game to go back to editing.

## If something goes wrong

- **Nothing moves**: check that the path in `scripts` matches the file location, and look at the output console. A JavaScript error is logged with the script path and the function name, and it never crashes the engine.
- **The Actor moves too fast or too slow**: you are multiplying by `dt`, so the number is a speed in units per second.

## Going further

- [Your first script](tutorials.html?tutorial=your-first-script) goes deeper into the lifecycle.
- [Handling player input](tutorials.html?tutorial=handling-player-input) makes the Actor respond to the keyboard.
- The [Documentation](documentation.html) page covers the whole engine.
