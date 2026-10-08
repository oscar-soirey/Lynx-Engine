You can drive the whole animation system from JavaScript, with no C++ and no editor graph. This tutorial builds a complete hero: idle and run chosen by speed, and a hit reaction that cannot be interrupted. It relies on the rules explained in [How the animation system works](tutorials.html?tutorial=how-the-animation-system-works).

## What you will learn

- how to add animations, a blend space, states and transitions from a script
- how to feed the state machine from `Update`
- how to write conditions three different ways
- how to play a sound on a given frame

## Step 1: prepare the sprite sheets

An animation is a **horizontal strip** of images in your `assets/` folder. For this tutorial, imagine three of them:

- `hero_idle.png`: 4 frames
- `hero_run.png`: 8 frames
- `hero_hit.png`: 3 frames

## Step 2: create the component and the animations

Create a class for the hero. `AnimationSprite` is the component type name.

```js
// assets/classes/Hero.js
class Hero extends Actor {
    BeginPlay() {
        const anim = this.addComponent("AnimationSprite");
        this.anim = anim;

        //            name    texture          frames  loop   frameTime
        anim.addAnimation("idle", "hero_idle.png", 4);
        anim.addAnimation("run",  "hero_run.png",  8, true,  0.08);
        anim.addAnimation("hit",  "hero_hit.png",  3, false);
    }
}
```

Adding a first animation with `addAnimation` switches the component to state machine mode by itself. Remember that `loop = false` makes an animation finish: that is what lets the hit state end.

## Step 3: choose idle or run with a blend space

A blend space picks an animation from a number. Samples are `[position, animationName]`:

```js
anim.addBlendSpace("move", [[0, "idle"], [6, "run"]]);
```

A speed near 0 plays `idle`, a speed near 6 plays `run`, and the nearest sample wins in between. Nothing is interpolated: you switch between two strips.

## Step 4: add the states

A state plays an animation or a blend space. The `variable` option tells the state which parameter feeds the blend space:

```js
anim.addState("move", "move", { variable: "speed" });
anim.addState("hit", "hit", {
    interruptible: false,            // cannot be cut before it ends
    next: "move",                    // and then goes back to moving
    onEnter: () => console.log("ouch"),
});
anim.setDefaultState("move");
```

Use `interruptible: false` only on states that end. A looping state with that flag would lock the machine forever.

## Step 5: add the transitions

Only one is needed here: being hurt can happen from any state.

```js
anim.addAnyTransition("hit", { trigger: "hurt" }, 10);   // (to, condition, priority)
```

Because `hit` has a `next` state, it returns to `move` on its own when it ends. If you prefer to be explicit, you can also write:

```js
anim.addTransition("hit", "move", s => s.finished, 0, true);   // (from, to, condition, priority, waitFinished)
```

## Step 6: three ways to write a condition

All of these are accepted wherever a condition is expected:

```js
// 1. a function receiving the sprite
anim.addTransition("move", "jump", s => s.getBool("airborne"));

// 2. a comparison object (> >= < <= == !=)
anim.addTransition("move", "crouch", { param: "height", op: "<", value: 0.5 });

// 3. a shortcut
anim.addTransition("jump", "move", { isFalse: "airborne" });
anim.addAnyTransition("hit", { trigger: "hurt" }, 10);
```

Higher priority wins when several transitions are true on the same update. On a tie, the first declared wins.

## Step 7: feed the state machine

The machine only reacts to parameters, so update them every tick:

```js
Update(dt) {
    this.anim.setFloat("speed", Math.abs(this.velocity.x));
}

TakeDamage(amount) {
    this.anim.trigger("hurt");      // or setTrigger("hurt")
}
```

A trigger lasts a single update. If the hero is in an uninterruptible state when it arrives, it is lost, so do not rely on a trigger to be "remembered".

`setBool`, `setInt`, `getFloat`, `getBool` and `currentState` are also available.

## Step 8: react to frames

For a footstep sound, add an event on a frame of the animation (frames start at 1):

```js
anim.onAnimationFrame("run", 3, () => this.getComponent("SoundSource")?.play());
anim.onAnimationFinished("hit", () => console.log("hit finished"));
```

## The complete hero

```js
class Hero extends Actor {
    BeginPlay() {
        const anim = this.anim = this.addComponent("AnimationSprite");

        anim.addAnimation("idle", "hero_idle.png", 4);
        anim.addAnimation("run",  "hero_run.png",  8, true, 0.08);
        anim.addAnimation("hit",  "hero_hit.png",  3, false);
        anim.addBlendSpace("move", [[0, "idle"], [6, "run"]]);

        anim.addState("move", "move", { variable: "speed" });
        anim.addState("hit", "hit", { interruptible: false, next: "move" });
        anim.setDefaultState("move");

        anim.addAnyTransition("hit", { trigger: "hurt" }, 10);
        anim.onAnimationFrame("run", 3, () => this.getComponent("SoundSource")?.play());
    }

    Update(dt) {
        this.anim.setFloat("speed", Math.abs(this.velocity.x));
    }

    TakeDamage(n) {
        this.anim.trigger("hurt");
    }
}
```

## Pausing

`anim.stopAll()` pauses the whole machine and keeps the current state and frame. `anim.resumeAll()` continues from there. Animations also follow the game time, so a time dilation slows them down, which makes hit-stop effects look right.

## Going further

- [Building an Anim Graph in the editor](tutorials.html?tutorial=building-an-anim-graph) moves everything above into a visual asset.
- [Spawning and finding Actors](tutorials.html?tutorial=spawning-and-finding-actors) shows `actor.call("TakeDamage", 10)`, which is how another Actor triggers `TakeDamage`.
