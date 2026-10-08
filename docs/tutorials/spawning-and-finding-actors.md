Most games create and remove objects while they run: bullets, enemies, pickups. This tutorial uses the `Level` object to spawn Actors, find them again and make them talk to each other.

## What you will learn

- how to spawn an Actor from a script
- how to give Actors a limited lifetime
- how to find Actors by id or by tag
- how to call a function on another Actor

## Step 1: spawn an Actor

`Level.spawn(className, transform?)` creates an Actor from its class name. The second argument is optional and can be a position or a full transform. It returns the new Actor, or `null` if the class does not exist.

```js
const enemy = Level.spawn("Enemy", {
  position: { x: 12, y: 2, z: 0 },
  rotation: { x: 0, y: 0, z: 0 },
  scale:    { x: 1, y: 1, z: 1 }
});

if (!enemy)
  console.error("Could not spawn Enemy");
```

The class name must exist in the factory: a class defined in your game module, or an Actor class defined in JavaScript.

## Step 2: give it a lifetime

Setting `lifetime` makes an Actor destroy itself after that many seconds. It is the easiest way to clean up bullets and effects. Put this in the spawned Actor's own script:

```js
// scripts/Bullet.js
function BeginPlay() {
  parent.velocity = vec3(12, 0, 0);
  parent.lifetime = 2;
}
```

## Step 3: shoot when the player attacks

Combine spawning with input. This script spawns an Actor at its parent's position each time `attack` is pressed:

```js
// scripts/Shooter.js
function Update(dt) {
  if (Input.pressed("attack"))
    Level.spawn("Bullet", parent.position);
}
```

Create the `attack` action in Input Settings first (see [Handling player input](tutorials.html?tutorial=handling-player-input)).

## Step 4: tag and find Actors

Tags let you group Actors without knowing their ids.

```js
// scripts/Enemy.js
function BeginPlay() {
  parent.addTag("enemy");
}
```

From any script:

```js
const player  = Level.find("Player");           // by id, or null
const enemies = Level.findWithTag("enemy");      // array of Actors
const count   = Level.count("Enemy");            // by class name
```

Do not search every frame if you can avoid it. Look an Actor up once in `BeginPlay()` and keep the reference, and check `valid` before using it later.

## Step 5: talk to another Actor

`actor.call(name, ...args)` calls the function with that name in the scripts attached to the Actor. Define the function in the target's script:

```js
// scripts/Health.js
let hp = 100;

function TakeDamage(amount) {
  hp -= amount;
  if (hp <= 0)
    parent.destroy();
}
```

And call it from somewhere else:

```js
const player = Level.find("Player");
if (player && player.valid)
  player.call("TakeDamage", 10);
```

If several scripts on the same Actor define the same function, all of them are called, and the last return value is the result.

## Step 6: destroy safely

`parent.destroy()` and `Level.destroy(actor)` do not remove the Actor instantly: the request is applied after the engine finishes the current iteration, so you can destroy things from inside `Update` without breaking anything. After that, the Actor's `valid` becomes `false`.

## Going further

- Read the full `Level` reference in the [API page](api.html).
- [Your first C++ Actor](tutorials.html?tutorial=your-first-cpp-actor) shows how to create classes you can spawn.
