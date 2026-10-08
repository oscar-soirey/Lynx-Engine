In this tutorial you build a complete guard: it patrols between two points, spots the player, chases them, attacks, and goes back to patrolling when the player escapes. It uses one `.bt` file, one JavaScript class and one custom task. If you are new to the model, read [How behavior trees work](tutorials.html?tutorial=how-behavior-trees-work) first.

## What you will learn

- how to declare Blackboard keys
- how to build a tree with a Selector, Sequences, decorators and a service
- how to write the attack as a JavaScript task
- how to connect the AI to your animations

## Step 1: plan the behavior

Say it in order of priority, from the most important to the least:

1. **If I can see a target**: walk to it, then attack, then wait a moment.
2. **Otherwise**: walk to point A, wait, walk to point B, wait.

Priorities map to a **Selector**: the top child is tried first, and the next one is the fallback. Each branch is a **Sequence**, because each is a list of steps.

```text
Root
 └ Selector            (service: FindNearest → target)
    ├ Sequence         [Blackboard: target isSet, abort both]   CHASE
    │   ├ MoveTo target
    │   ├ Script Attack
    │   └ Wait 0.8
    └ Sequence                                                   PATROL
        ├ MoveTo patrol_a
        ├ Wait 1.5
        ├ MoveTo patrol_b
        └ Wait 1.5
```

## Step 2: create the tree

In the Content Browser, create a **Behavior Tree** asset: `assets/ai/Guard.bt`. Open it and declare the Blackboard keys:

- `target`: type `actor`
- `patrol_a`: type `vector`, default `-6 0 0`
- `patrol_b`: type `vector`, default `6 0 0`

Vector defaults are written as three numbers. Then build the tree from the diagram above.

## Step 3: the service that finds the player

Add a **FindNearest** service to the Selector:

- `tag`: `player`
- `radius`: `12`
- `result`: `target`
- `interval`: `0.3`

A service runs at its interval while its node is active. The Selector is always active, so every 0.3 seconds the guard looks for the nearest actor tagged `player` within 12 units. It writes it into `target`, or clears `target` if nobody is there. That one line replaces all the "can I see the player?" code.

The player must carry the tag. In the player's script:

```js
function BeginPlay() {
    parent.addTag("player");
}
```

## Step 4: the decorator that switches branches

On the **CHASE** Sequence, add a **Blackboard** decorator:

- `key`: `target`
- `op`: `isSet`
- `abort`: `both`

Now three things happen automatically:

- when `target` is not set, the decorator is false, so the chase branch fails at once and the Selector falls back to the patrol;
- while patrolling, as soon as `target` becomes set, the higher branch is valid again, and `lowerPriority` **interrupts** the patrol;
- while chasing, if `target` is cleared (the player left, or was destroyed), `self` **stops** the chase.

Without `abort`, the guard would finish its whole patrol walk before noticing the player.

## Step 5: configure the movement

The **MoveTo** node moves the actor toward a Blackboard key (a vector or an actor) and succeeds when it is close enough. Settings for the chase:

- `target`: `target`
- `speed`: `4`
- `acceptance`: `1.5` (the distance that counts as arrived, which is the attack range)
- `anim_speed_param`: `speed`

For the patrol moves, set `target` to `patrol_a` and `patrol_b`, and keep the same speed.

Things MoveTo does for you:

- it moves on X and Y only, unless `use_z` is on;
- if the actor has a collider, it moves with collisions;
- it flips the actor toward the movement (`face`);
- it **fails** if the actor stays blocked for one second, or after `timeout` seconds if you set one;
- with `anim_speed_param`, it writes the actual speed into that parameter of the `AnimationSprite`. If your guard uses an Anim Graph with a `speed` parameter (see [Building an Anim Graph](tutorials.html?tutorial=building-an-anim-graph)), walking and idling are animated with no extra code.

## Step 6: write the attack task

Add a **Script** task to the chase Sequence, with class `Attack`, and two extra attributes: `duration` (for example `0.4`) and `damage` (for example `10`). Then create `assets/ai/Attack.js`:

```js
class Attack extends BTTask {
    Execute() {
        const anim = this.owner.getComponent("AnimationSprite");
        if (anim) anim.setTrigger("attack");

        const target = this.blackboard.get("target");
        if (target && target.valid)
            target.call("TakeDamage", this.params.damage);

        this.t = 0;
        return BT.Running;
    }

    Tick(dt) {
        this.t += dt;
        return this.t >= this.params.duration ? BT.Success : BT.Running;
    }

    Abort() {
        // the chase was interrupted: nothing to clean up here
    }
}
```

How it works:

- `Execute()` runs once when the task starts. It returns `BT.Running` because the attack takes time.
- `Tick(dt)` is called on every tick until it returns `BT.Success`.
- `Abort()` is called if something interrupts the task, for example the `self` abort of the decorator, or `stop()`.
- `this.owner` is the guard, `this.blackboard` its memory, and `this.params` holds the other attributes of the node in the file, so `duration` and `damage` can be tuned in the editor.

The target is an actor, so `target.call("TakeDamage", n)` reaches the `TakeDamage` function of its scripts.

## Step 7: why Wait, and not Cooldown?

After the attack, the Sequence has a `Wait 0.8`. You might be tempted to put a **Cooldown** decorator on the attack instead. But for some time after its node ends, a Cooldown makes that node **fail**. A failing attack would make the whole chase Sequence fail, and the Selector would fall through to the patrol while the player stands right there. A `Wait` is a step that succeeds after a delay, which is exactly the pause you want.

## Step 8: add the guard to the game

```js
// assets/classes/Guard.js
class Guard extends Actor {
    BeginPlay() {
        this.addTag("guard");
        this.addComponent("AnimationSprite", { graph: "anim/Guard.animgraph" });
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Guard.bt" });
    }

    TakeDamage(n) {
        console.log("guard hit for", n);
    }
}
```

The `AI` component starts by itself. `Guard.animgraph` is a graph like the one in [Building an Anim Graph](tutorials.html?tutorial=building-an-anim-graph), with a `speed` float parameter and an `attack` trigger. Place a `Guard` with **Place Actors**, press Play, and open `Guard.bt` while the game is running: the active nodes are green and the Blackboard values update live.

## Step 9: tuning and troubleshooting

- **The guard never notices me**: check that the player has the `player` tag and is within `radius`, and look at the Blackboard in the debug view. If `target` never gets set, the service is the problem.
- **The guard keeps chasing for a moment after the player leaves**: `target` is cleared on the next service tick, every 0.3 seconds here. Lower the `interval` for a faster reaction.
- **The guard stops walking**: a `MoveTo` blocked by a wall fails after a second, which sends the tree back to its start.
- **Everything restarts each time**: that is normal. When the tree ends, the Root restarts it on the next tick.

## Going further

- [Custom behavior tree nodes in JavaScript](tutorials.html?tutorial=custom-behavior-tree-nodes-in-javascript) writes conditions and services of your own, such as line of sight.
- [How the animation system works](tutorials.html?tutorial=how-the-animation-system-works) explains why the `attack` trigger must not arrive during an uninterruptible state.
