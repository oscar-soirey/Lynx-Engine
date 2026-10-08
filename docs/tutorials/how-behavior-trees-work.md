A behavior tree describes what an AI wants to do as a tree of small steps. This tutorial explains the model used by Lynx (`.bt` files, run by the `AI` component): how the tree is walked, what success and failure mean, and what decorators, services and the Blackboard are for. It is based on the engine source.

## What you will learn

- how the tree is executed on every tick
- the difference between a Selector and a Sequence
- what a decorator does, and how "abort" works
- what services and the Blackboard are

## Step 1: the vocabulary

A tree is made of **nodes** connected from top to bottom.

- **Root**: the entry. It has one child, and the tree restarts when it finishes.
- **Composites** (`Selector`, `Sequence`): they run their children.
- **Tasks**: the leaves. They do something (`Wait`, `MoveTo`, `Log`, `SetValue`...) or run your own code (`CallFunction`, `Script`).
- **Decorators**: conditions or modifiers attached to a node.
- **Services**: small recurring jobs attached to a node.

The order of the children is their **vertical position in the editor**: the top one first. The number in a node's title shows its order.

## Step 2: three results

Every node ends with one of three results: **Success**, **Failure** or **Running** (it needs more time). A `Wait` is Running until its time is over, then Success. A `MoveTo` is Running until the actor arrives, then Success, or Failure if it times out or stays blocked against a wall for a second.

Running travels up the tree: while a task is Running, every node above it is Running too.

## Step 3: Selector and Sequence

These two composites contain most of the logic.

A **Selector** is a fallback list. It runs its children from the top and stops at the first one that **succeeds**. If a child fails, it tries the next. If all fail, the Selector fails. Think of it as "try this, otherwise that".

A **Sequence** is a list of steps. It runs its children from the top and stops at the first one that **fails**. If all succeed, the Sequence succeeds. Think of it as "do this, then that, then that".

```text
Root
 └ Selector                      "do the first thing that works"
    ├ Sequence                   "attack": all steps must work
    │   ├ MoveTo target
    │   └ Attack
    └ Wait 2                     otherwise, rest
```

## Step 4: what happens on each tick

On every tick the tree starts again from the Root, but it does not start over. A Running task is **resumed**: each composite remembers which child it was running. Only when the tree ends (success or failure) does the Root restart from the beginning on the next tick.

In order, each tick:

1. checks the **observer aborts** (see step 6);
2. runs the active path from the Root, ticking the **services** (see step 7) of each active node on the way.

## Step 5: decorators

A decorator is attached to a node and changes how it runs. When the node is about to start, its decorator conditions are checked. If one is false, the node **fails immediately**, and its parent reacts as to any failure.

The decorators are:

- `Blackboard`: a condition on a Blackboard key (`isSet`, `isNotSet`, `==`, `!=`, `>`, `>=`, `<`, `<=`)
- `CallFunction`: a condition from a JavaScript function of the actor
- `Script`: a condition from your own `BTDecorator` class
- `Cooldown`: after the node ends, it fails for some time
- `Loop`: runs the node again while it succeeds (`count`, with 0 meaning forever)
- `TimeLimit`: the node fails if it runs for too long
- `ForceSuccess`: the node always succeeds

The conditions have an `inverse` option to flip them.

## Step 6: observer aborts

A condition is only checked when a node starts, unless you set its **abort** option. This is what makes an AI reactive.

- `self`: while the node runs, if its condition becomes false, the node stops (and fails).
- `lowerPriority`: in a Selector, if a branch **above** the running one becomes valid again, the running branch is interrupted and the higher branch takes over.
- `both`: both behaviors.

Typical use: a guard patrols (a low-priority branch). As soon as a target appears, the chase branch above it becomes valid. With `lowerPriority`, the patrol is cut immediately instead of finishing its walk. With `self`, the chase stops as soon as the target disappears.

## Step 7: services

A service runs at a regular **interval** while its node is active. It is how an AI senses the world without cluttering the tree. Built-in services:

- `FindNearest`: finds the nearest actor with a tag, inside a radius, and writes it into an actor key (or clears the key).
- `DistanceTo`: writes the distance to a target into a float key.
- `CallFunction` and `Script`: your own code.

Put a service on a node that is always active, such as the top Selector, so it keeps running.

## Step 8: the Blackboard

The Blackboard is the memory of an AI: named values shared by the whole tree. Keys have a type (`bool`, `int`, `float`, `string`, `vector`, `actor`) and an optional default value, both declared in the editor. A key holding an actor that has been destroyed counts as **not set**.

```js
const bb = this.ai.blackboard;
bb.set("target", Level.find("player"));   // actor, number, bool, text or {x, y, z}
bb.get("hp", 100);                         // with a default value
bb.has("target");
bb.clear("target");
```

Decorators read it, services and tasks write it, and your JavaScript can do both.

## Step 9: running a tree

Add the `AI` component to an actor and point it at a `.bt` file:

```js
class Guard extends Actor {
    BeginPlay() {
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Guard.bt" });
    }
}
```

The component starts by itself (`startOnBegin` is true). It offers `start()`, `stop()`, `restart()` and `load(asset)`. `stop()` sends an `Abort` to the running task. `running`, `activeNodes` and `loadedTree` let you read its state.

In C++: `actor->AddComponent<lynx::BehaviorTreeComponent>().behavior_tree = "ai/Guard.bt";`, then `GetBlackboard().SetActor("target", player)`.

## Step 10: watch it think

With the game running, an open `.bt` shows the active nodes in green and the live Blackboard values. Use the *Debug* list of the toolbar to choose which actor to watch. This is the fastest way to understand why an AI does something strange.

## Going further

- [A guard with a behavior tree](tutorials.html?tutorial=a-guard-with-a-behavior-tree) builds a complete example.
- [Custom behavior tree nodes in JavaScript](tutorials.html?tutorial=custom-behavior-tree-nodes-in-javascript) writes your own tasks, decorators and services.
