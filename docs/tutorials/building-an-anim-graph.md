An **Anim Graph** (`.animgraph`) is the visual way to build the same machine you would otherwise write in code. You assemble animations, a blend space and a state machine in the editor, save an asset, and give its path to a sprite. This tutorial builds a hero with idle, run and a hit reaction.

## What you will learn

- how an Anim Graph is organized, starting from its output
- how to create parameters, animations, states and transitions
- how to use notifies to call your JavaScript
- how to read and debug the result

## Step 1: think from the output

The graph works like the AnimGraph of Unreal Engine: you start from the **end**. The final node is **Output Pose**, the pose the sprite plays. You plug one of three things into it:

- an **Animation**: one sprite sheet;
- a **Blend Space**: a number picks the animation;
- a **State Machine**: states and transitions.

```text
[Locomotion (State Machine)] ────► [Output Pose]
```

If nothing is plugged into Output Pose, the sprite plays nothing, and the console prints a message saying so.

## Step 2: create the asset

In the **Content Browser**, create a new file of type **Anim Graph**, then double-click it to open it. Give it a name such as `Hero.animgraph`, in a folder like `assets/anim/`.

## Step 3: declare the parameters

Parameters are the values your game gives to the graph. There are four types:

- `float`, for example `speed`
- `bool`, for example `airborne`
- `int`
- `trigger`, for example `hurt`: a pulse that lasts one update

Create `speed` (float) and `hurt` (trigger).

## Step 4: add the animations

An animation is a horizontal sprite sheet. For each one, set:

- `texture`: the strip image, relative to `assets/`
- `frames`: how many images are in the strip
- `frame time`: seconds per image
- `loop`: whether it repeats

Create `Idle` (4 frames), `Run` (8 frames) and `Hit` (3 frames, **loop off** so it can finish).

### Notifies

An animation can have **notifies**: a frame number (starting at 1) and the name of a JavaScript function. When the animation enters that frame, Lynx calls the function of that name on the actor. On `Run`, add a notify on frame 3 and another on frame 7 named `OnFootstep`.

## Step 5: build the state machine

Add a **State Machine** named `Locomotion` and plug it into Output Pose. Open it with a double-click. It contains:

- **Entry**: points to the first state. Connect it to `Idle`.
- **Any State**: transitions from here are tested from every state (hurt, death...).
- the **states** and their **transitions**.

Create three states: `Idle`, `Run` and `Hit`. Each state has its own small graph (double-click it): an Animation plugged into its own Output Pose. Use `Idle`, `Run` and `Hit` respectively.

State options:

- `interruptible`: turn it **off** on `Hit`, so nothing cuts the reaction.
- `next`: set it to `Idle` on `Hit`. When the animation ends and no transition fires, the machine goes there.
- `restart on enter`: usually left on.
- `on enter` and `on exit`: names of JavaScript functions to call.

## Step 6: add the transitions

Drag the pin of one state onto another. Each transition has conditions, and **all of them must be true** (they are combined with AND). A transition with no conditions always fires.

The condition kinds are:

- `compare`: a parameter against a number, with `>`, `>=`, `<`, `<=`, `==` or `!=`
- `isTrue` and `isFalse`: a bool parameter
- `trigger`: a trigger parameter
- `finished`: the current animation has ended
- `script`: a JavaScript function of the actor that returns `true` or `false`

Create:

- `Idle` → `Run` when `speed > 0.1`
- `Run` → `Idle` when `speed < 0.1`
- **Any State** → `Hit` when the trigger `hurt` is set, with **priority 10**

When several transitions are true at once, the highest priority wins. On a tie, the first one wins. `wait_finished` makes a transition wait for the end of the current animation.

## Step 7: a blend space instead of two states

If you would rather have a single `Move` state, create a **Blend Space** with a `variable` (`speed`) and two samples: position `0` plays `Idle` and position `6` plays `Run`. Plug the blend space into the state's Output Pose. The state follows `speed` by itself, and there is no need for the `Idle` ⇄ `Run` transitions.

A blend space picks the **nearest** sample. It does not mix images.

## Step 8: use it from a script

Give the graph to an `AnimationSprite` and send it parameters:

```js
class Hero extends Actor {
    BeginPlay() {
        this.anim = this.addComponent("AnimationSprite", { graph: "anim/Hero.animgraph" });
    }

    Update(dt) {
        this.anim.setFloat("speed", Math.abs(this.velocity.x));
    }

    TakeDamage(n) {
        this.anim.setTrigger("hurt");
    }

    OnFootstep() {                       // the notify of the Run animation
        this.getComponent("SoundSource")?.play();
    }

    CanAttack() {                        // a "script" condition
        return this.stamina > 10;
    }
}
```

You can also set the `graph` property of the component in **Details**, change graphs at runtime with `anim.loadGraph("anim/Boss.animgraph")` (it returns `false` if the file cannot be read), and read `anim.loadedGraph`.

In C++: `sprite.graph = "anim/Hero.animgraph";` (or `LoadGraph`), then `SetFloat` and `SetTrigger`. The `on_graph_event` callback of the component receives the notifies and state enter and exit events, in addition to your JavaScript functions.

## Step 9: debug it live

While the game runs, an open `.animgraph` highlights the state that is playing, and shows the values of the parameters in real time. A list in the toolbar chooses which actor you are watching.

## The file is plain XML

The editor saves your graph as XML, which is handy for version control and for understanding what you built. A trimmed version of this hero looks like this:

```xml
<AnimGraph version="2">
  <Output source="1"/>
  <Parameter name="speed" type="float" default="0"/>
  <Parameter name="hurt" type="trigger" default="0"/>

  <Animation name="Idle" texture="hero_idle.png" frames="4" frame_time="0.15" loop="true"/>
  <Animation name="Run" texture="hero_run.png" frames="8" frame_time="0.08" loop="true">
    <Notify frame="3" event="OnFootstep"/>
    <Notify frame="7" event="OnFootstep"/>
  </Animation>
  <Animation name="Hit" texture="hero_hit.png" frames="3" frame_time="0.1" loop="false"/>

  <PoseNode id="1" type="statemachine" source="Locomotion"/>
  <StateMachine id="2" name="Locomotion" entry="Idle">
    <State id="3" name="Idle" motion="Idle"/>
    <State id="4" name="Run" motion="Run"/>
    <State id="5" name="Hit" motion="Hit" interruptible="false" next="Idle"/>

    <Transition id="6" from="Idle" to="Run">
      <Condition kind="compare" param="speed" op="&gt;" value="0.1"/>
    </Transition>
    <Transition id="7" from="Run" to="Idle">
      <Condition kind="compare" param="speed" op="&lt;" value="0.1"/>
    </Transition>
    <Transition id="8" from="*" to="Hit" priority="10">
      <Condition kind="trigger" param="hurt"/>
    </Transition>
  </StateMachine>
</AnimGraph>
```

`from="*"` is the Any State. The positions of the nodes in the editor are saved too; they are left out here. Older files with states directly at the root still open: they are read as a state machine named `Locomotion` plugged into Output Pose.

## Going further

- [How the animation system works](tutorials.html?tutorial=how-the-animation-system-works) explains the exact order of every update.
- The `MoveTo` task of a behavior tree can write the actor's speed into a graph parameter. See [A guard with a behavior tree](tutorials.html?tutorial=a-guard-with-a-behavior-tree).
