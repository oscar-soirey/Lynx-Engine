This tutorial explains what happens inside the animation system, from the sprite sheet up to the state machine. It is based on the engine source (`AnimationSystem.h` and `AnimationSystem.cpp`). Once you know the rules, the editor, JavaScript and C++ APIs all become predictable, because they sit on top of the same classes.

## What you will learn

- the three building blocks: `animation`, `blend_space` and `anim_manager`
- exactly how frames, events and loops behave
- how the state machine picks the next state on every update
- the traps that cost people an afternoon

## Step 1: the big picture

Everything the manager can play implements one small interface called `playable`: `play()`, `restart()`, `update(dt)`, `is_finished()` and an optional `set_value(float)`. Two classes implement it:

- `animation` plays one horizontal sprite sheet.
- `blend_space` chooses between several animations according to a number.

Above them, `anim_manager` is the state machine. It owns **parameters**, **states** (each state points to a playable) and **transitions**.

```text
parameters (speed, airborne, hurt...)
      │
      ▼
anim_manager ── states ──► playable ──► animation  (a sprite sheet)
      │                       └──────► blend_space (picks an animation)
  transitions
```

You almost never create these classes yourself. `AnimationSpriteComponent` creates and owns them, and fills them from your code or from an `.animgraph` file. But every behavior below comes from them.

## Step 2: an animation is a sprite sheet

An `animation` takes a texture, a frame count, a loop flag and the time of one frame. The texture is a **horizontal strip**: `frame_count` images side by side. The animation shows frame `n` by moving the UV region of the sprite, so the whole sheet stays one texture.

On every `update(dt)` the animation adds `dt` to an internal clock. Each time the clock passes the frame time, it moves to the next frame. A long frame (a lag spike, for example) advances several frames at once instead of losing time.

When the last frame is passed:

- with `loop = true`, it goes back to the first frame;
- with `loop = false`, it stays on the last frame, becomes **finished**, fires its events and calls its `on_finished` callback.

## Step 3: frame events

An event is a function called when the animation **enters** a frame:

```cpp
auto& run = sprite.AddAnimation("run", "hero_run.png", 8, true, 0.08f);

run.add_event(3, [] { /* left foot touches the ground */ });
run.add_event(7, [] { /* right foot touches the ground */ });
```

Two details matter:

- **Frames are numbered from 1** in the API. Frame 1 is the first image. (Internally, `current_frame()` counts from 0.)
- The events of frame 1 fire on the **first update after a restart**, and again each time a loop wraps around.

Several callbacks can share a frame. `clear_events()` removes all of them, `clear_events(frame)` removes the ones of one frame. An event on a frame that does not exist (below 1 or above the frame count) is ignored.

In the editor, these events are called *notifies*: a frame and the name of a JavaScript function on the actor.

## Step 4: a blend space is a selector

Despite the name, a `blend_space` does **not** interpolate between images. It holds samples (a position and an animation) sorted by position, and a current value. On each update it plays the sample nearest to the value:

- a value at or below the first sample plays the first animation;
- a value at or above the last sample plays the last one;
- in between, it plays the closer of the two neighbors.

```text
samples:   0 → idle      6 → run
value 0.0  → idle
value 2.9  → idle
value 3.0  → run      (halfway: the upper sample wins)
value 9.0  → run
```

Only the selected animation is updated. The others keep the frame they had, and nothing is restarted when the selection changes.

The value comes from the manager. A state can name a parameter in its `variable` option; on every update the manager reads that parameter and calls `set_value()` on the state's playable. That is how a `speed` parameter drives a walk and run blend space.

## Step 5: parameters and triggers

The manager stores `bool`, `int` and `float` parameters, and all three are stored as floats. `get_bool` is just "not zero". Reading a parameter that was never set returns 0.

A **trigger** is different. `set_trigger("hurt")` stays active until the **end of the next update**, then it is cleared, whether or not a transition used it. It is a one-update pulse.

## Step 6: states and their options

`add_state(name, playable, options)` registers a state. The options:

- `interruptible` (default true): if false, no transition can fire until the state has finished.
- `restart_on_enter` (default true): restarts the playable when the state starts.
- `next_state`: the state to play when this one finishes and no transition fired. Empty means the default state.
- `variable`: the parameter sent to the playable with `set_value()` (see step 4).
- `on_enter` and `on_exit`: callbacks.

## Step 7: what one update really does

This is the heart of the system. On every `update(dt)`, the manager does the following, in this order:

1. If animations are stopped (`StopAllAnimations()`), do nothing at all.
2. If there is no current state, enter the default state. If there is still none, clear the triggers and stop.
3. Compute `finished` (the current playable has finished) and `locked` (the state is not interruptible **and** not finished).
4. If not locked, look at every transition and keep the best one:
   - it must come from the current state, or be an *any* transition (empty `from`);
   - a transition to the state you are already in is skipped, unless that state has finished (this is how an attack can retrigger itself);
   - if `wait_finished` is set, the state must have finished;
   - its condition must be true (no condition means always true);
   - the highest `priority` wins, and on a tie the one declared **first** wins.
5. If a transition won, switch to its target. Otherwise, if the state finished, switch to `next_state`, or to the default state if there is none (and it is a different state).
6. Send the `variable` to the current playable, then update it with `dt`.
7. Clear the triggers.

Switching state calls `on_exit` of the old state, restarts it if it was finished (so it is ready next time), makes the new state current, restarts it if `restart_on_enter` is set (or if it was already finished), calls `play()` and then `on_enter`.

`force_state(name)` switches immediately and ignores all the rules above, including the lock.

## Step 8: the traps

- **A non-interruptible looping state locks the machine forever.** A looping animation never finishes, so `interruptible = false` should only be used on states that end (a hit, an attack).
- **A trigger sent while the state is locked is lost.** Step 4 is skipped, but step 7 still clears it. If a `hurt` trigger arrives during an uninterruptible attack, it disappears.
- **Unknown state names are ignored silently.** `force_state("runn")` does nothing. Check the spelling when a transition seems dead.
- **A finished state with nothing to do stays on its last frame.** Give it a `next_state`, a default state or a transition.
- **A transition without conditions fires right away.** That is useful with `wait_finished` ("after this, go there") and dangerous without it.

## Step 9: putting it together in C++

```cpp
auto& sprite = actor->AddComponent<lynx::AnimationSpriteComponent>();
sprite.size = {2.f, 2.f};

sprite.AddAnimation("idle", "hero_idle.png", 4);
auto& run = sprite.AddAnimation("run", "hero_run.png", 8, true, 0.08f);
sprite.AddAnimation("hit", "hero_hit.png", 3, false, 0.1f);

run.add_event(3, [] { /* footstep */ });

lynx::anim_state_options hit_options;
hit_options.interruptible = false;   // plays until the end
hit_options.next_state = "idle";     // then back to idle

sprite.AddState("idle", "idle");
sprite.AddState("run", "run");
sprite.AddState("hit", "hit", hit_options);
sprite.SetDefaultState("idle");

auto& m = sprite.GetAnimManager();
m.add_transition("idle", "run", m.greater("speed", 0.1f));
m.add_transition("run", "idle", m.less("speed", 0.1f));
m.add_any_transition("hit", m.triggered("hurt"), 10);

sprite.SetFloat("speed", 3.f);   // from your gameplay code
sprite.SetTrigger("hurt");
```

Ready-made conditions are `is_true`, `is_false`, `greater`, `less` and `triggered`. For anything else, a condition is just a function taking the manager and returning a `bool`.

## Going further

- [Animation state machines in JavaScript](tutorials.html?tutorial=animation-state-machines-in-javascript) builds the same hero without C++.
- [Building an Anim Graph in the editor](tutorials.html?tutorial=building-an-anim-graph) does it visually.
