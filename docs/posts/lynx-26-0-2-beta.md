Small number, big change: **Lynx 26.0.2 Beta** brings the first JavaScript integration.

## Gameplay in JavaScript

Lynx is a C++ engine, but you should not have to touch C++ to make a game. 26.0.2 is where that promise starts to become real: scripts run on **QuickJS-ng**, and they are plain `.js` files attached to your Actors.

A script is a *behavior*, not a class. You write the lifecycle functions you need, and `parent` is the Actor the script is attached to:

```js
function BeginPlay() {
  console.log("Hello from", parent.id);
}

function Update(dt) {
  parent.position.x += 2 * dt;
}
```

## This is only the first step

"First integration" means exactly that: the foundation is there, and it will grow in the next releases. If something you need is missing from the API, tell me. Bridges are added when they are needed.

## Where to start

- the [JavaScript guide](scripting.html) for the whole model
- the [Your first script](tutorials.html?tutorial=your-first-script) tutorial
- the [API reference](api.html) if you just want to check a function

Full changelog: [v26.0.1...v26.0.2](https://github.com/oscar-soirey/Lynx-Engine/compare/v26.0.1...v26.0.2)
