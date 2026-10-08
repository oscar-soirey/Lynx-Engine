Scripts are the fastest way to make something happen in Lynx. This tutorial walks through the script model and the three lifecycle functions.

## What you will learn

- how a script differs from a class
- when `BeginPlay`, `Update` and `EndPlay` run
- how to read and change an Actor from a script
- how to keep a script safe when Actors disappear

## Step 1: a script is a behavior

When Lynx loads a JavaScript file, it wraps it in a function that receives `parent`, the Actor the script is attached to. The engine then looks up the lifecycle functions by name.

```js
// scripts/Spinner.js
function BeginPlay() {
  console.log("Spinner started on", parent.id);
}

function Update(dt) {
  parent.transform.rotation.z += 90 * dt;
}

function EndPlay() {
  console.log("Spinner stopped");
}
```

You never write `new` and you never declare a class. The engine creates one behavior instance per attachment, so two Actors using `Spinner.js` each have their own variables.

## Step 2: know the lifecycle

- `BeginPlay()` is called when the game starts. Use it for initialization.
- `Update(dt)` is called every tick. Use it for movement, logic and input.
- `EndPlay()` is called on stop, removal or destruction. Use it for cleanup.

If a script is attached during gameplay, its `BeginPlay()` runs before its first `Update()`. All three functions are optional: define only what you need.

## Step 3: use variables for state

Variables declared at the top of the file live as long as the attachment. Use them to keep a timer, a counter or a direction.

```js
let elapsed = 0;

function Update(dt) {
  elapsed += dt;
  parent.position.y = 2 + Math.sin(elapsed * 3);
}
```

## Step 4: read and write the Actor

`parent` exposes the Actor's data directly. Changing `x`, `y` or `z` changes the Actor immediately.

```js
parent.position.x = 5;
parent.velocity = vec3(2, 0, 0);
parent.transform.scale = vec3(2);

// A detached copy that will not follow the Actor:
const start = parent.position.clone();
```

`vec3()` takes up to three numbers. With a single number, all three components use that value, so `vec3(2)` is `vec3(2, 2, 2)`.

## Step 5: log and debug

Use `console.log`, `console.info`, `console.warn` and `console.error`. They appear in the output console of the editor.

```js
console.warn("Low health on", parent.id);
```

If a script throws, the engine catches the error and logs it with the script path and the function name. Fix the file, then reload: the engine recreates the script instances from disk.

## Step 6: stay safe when Actors are destroyed

An Actor can be destroyed while you still hold a reference to it. Check `valid` before using a reference that may have outlived its Actor.

```js
function Update(dt) {
  if (!parent.valid)
    return;

  parent.position.x += 3 * dt;
}
```

The same goes for any Actor you got from `Level.find()`.

## Going further

- [Handling player input](tutorials.html?tutorial=handling-player-input)
- [Spawning and finding Actors](tutorials.html?tutorial=spawning-and-finding-actors)
- The complete [JavaScript guide](scripting.html) and [API reference](api.html)
