The built-in nodes cover a lot, but real games need their own rules: line of sight, low health, a special attack. In Lynx you write those as small JavaScript classes and drop them into the tree with a **Script** node. This tutorial covers the three kinds: tasks, decorators and services.

## What you will learn

- the three base classes: `BTTask`, `BTDecorator` and `BTService`
- what to return, and what each callback is for
- how to pass settings from the editor to your code
- when `CallFunction` is enough

## Step 1: where the code lives

Any `.js` file under `assets/` can hold a node class. In the tree, add a **Script** node (task, decorator or service) and set its `class` to the name of your class. The **JS** button in the Details panel can copy a skeleton for you.

Remember that **there is one instance of the class per node and per actor**. Two guards running the same tree do not share the fields of `this`.

Inside a node you always have:

- `this.owner`: the actor running the tree
- `this.blackboard`: its memory (`get`, `set`, `has`, `clear`)
- `this.params`: the other attributes of the node, as written in the editor or in the file
- `this.name`: the name of the class

## Step 2: tasks

A task does something over time. It extends `BTTask` and has three callbacks:

- `Execute(dt)`: called when the task starts
- `Tick(dt)`: called on every tick while it is Running
- `Abort()`: called if the task is interrupted (an abort from a decorator, or `stop()` on the `AI` component)

What to return, from `Execute` and `Tick`:

- `true`, nothing, or `BT.Success`: **success**
- `false` or `BT.Failure`: **failure**
- `BT.Running`: not finished, call `Tick` next time

Here is a task that flees from the target for a given duration:

```js
class Flee extends BTTask {
    Execute() {
        this.t = 0;
        const target = this.blackboard.get("target");
        if (!target || !target.valid)
            return BT.Failure;           // nobody to flee from
        return BT.Running;
    }

    Tick(dt) {
        const target = this.blackboard.get("target");
        if (!target || !target.valid)
            return BT.Success;

        const away = this.owner.position.x < target.position.x ? -1 : 1;
        this.owner.position.x += away * this.params.speed * dt;

        this.t += dt;
        return this.t >= this.params.duration ? BT.Success : BT.Running;
    }

    Abort() {
        this.owner.velocity.x = 0;
    }
}
```

In the tree: `<Node type="Script" class="Flee" speed="6" duration="2"/>`. The attributes `speed` and `duration` arrive in `this.params`, as numbers.

## Step 3: decorators

A decorator is a condition. It extends `BTDecorator` and implements `Check()`, which returns `true` or `false`.

```js
class LowHealth extends BTDecorator {
    Check() {
        return this.owner.hp < this.params.threshold;
    }
}
```

Attach it to a node as a **Script** decorator with `class` set to `LowHealth` and a `threshold` attribute. It has the same options as the other decorators:

- `inverse` flips the result;
- `abort` makes it reactive, as explained in [How behavior trees work](tutorials.html?tutorial=how-behavior-trees-work). With `abort` set to `both`, a flee branch placed above the chase branch interrupts it the moment health drops, and stops by itself when health recovers.

A decorator should be **cheap and have no side effects**: with an abort option it is checked on every tick.

## Step 4: services

A service is a recurring job that runs at an interval while its node is active. It extends `BTService`:

- `Activated()`: when the node becomes active
- `Tick(dt)`: at every interval
- `Deactivated()`: when the node stops

Example: line of sight. The service only writes to the Blackboard, and the tree reacts through a normal decorator.

```js
class Perception extends BTService {
    Tick(dt) {
        const player = Level.find("player");
        const range = this.params.range;

        const sees = player && player.valid
            && Math.abs(player.position.x - this.owner.position.x) < range;

        if (sees)
            this.blackboard.set("target", player);
        else
            this.blackboard.clear("target");
    }
}
```

As a **Script** service, set `class` to `Perception`, `interval` to `0.3` and `range` to `10`. This separation, with services writing facts and decorators reading them, keeps trees easy to read.

## Step 5: when CallFunction is enough

If your logic already lives on the actor, you do not need a class. **CallFunction** exists for tasks, decorators and services, and calls a method of the actor (or a function of one of its attached scripts) by name:

```js
class Guard extends Actor {
    Heartbeat(dt) {                    // CallFunction task: runs every tick
        this.pulse = (this.pulse || 0) + dt;
        return this.pulse > 3 ? true : "running";   // "running": not finished yet
    }

    IsAngry() {                        // CallFunction decorator
        return this.anger > 5;
    }
}
```

For a task, the return value works like a task's: `true` or nothing is success, `false` is failure, and the string `"running"` means it is not finished. Use a `Script` class when you need the state per node, `Abort()` or a service lifecycle.

## Step 6: tips

- **Do not store actors in fields without checking them.** Use `target.valid` before using a reference that may have outlived its actor, or read it again from the Blackboard each time.
- **Prefer the Blackboard to globals** for sharing data between nodes. It is visible in the debug view of the editor.
- **Do not block.** A task that needs time returns `BT.Running` and uses `Tick(dt)`. Anything else freezes the game.
- **Log with the node name.** `console.log(this.name, ...)` helps a lot when several nodes talk at once.
- **Errors do not crash the engine.** A JavaScript error is caught and logged, but a failing node will not do what you expect, so watch the console.

## Going further

- Combine the pieces in [A guard with a behavior tree](tutorials.html?tutorial=a-guard-with-a-behavior-tree).
- The full list of built-in nodes is in [How behavior trees work](tutorials.html?tutorial=how-behavior-trees-work).
