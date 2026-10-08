Behavior trees are not the only way to drive an AI. `AIBehavior` is a small C++ **state machine** for actors, built on the same principles as the animation manager. It is a good fit when your AI is a handful of clear states (patrol, chase, flee) and you prefer plain C++ to an editor graph. This tutorial builds an enemy brain with it.

## What you will learn

- the pieces of `AIBehavior`: actions, axes, parameters, states and transitions
- how a tick works, and how transitions are chosen
- how to wire it to an actor from a component
- when to choose it over a behavior tree

## Step 1: the model

`AIBehavior` sits on top of an actor. It does not move anything by itself; it calls **your** code. It has five concepts:

- **Actions**: named functions, like `"jump"` or `"attack"`, that you register and can trigger.
- **Axes**: named functions taking a float, like `"move_x"` (-1 left, +1 right). An axis value is **held**: the callback is called on every tick while the value is not 0, and one last time when it returns to 0.
- **Parameters**: named `bool`, `int` and `float` values, plus **triggers** that live for one tick.
- **States**: a name with `on_enter`, `on_update(dt)` and `on_exit` callbacks.
- **Transitions**: `from → to` when a condition is true, with a priority.

It lives in `AIBehavior.h`, in the `lynx` namespace, and takes the actor in its constructor: `lynx::AIBehavior ai(actor);`.

## Step 2: how a tick works

You call `Tick(dt)` yourself, once per game tick. Each call:

1. enters the default state if none is active yet;
2. adds `dt` to the **state time** (the seconds spent in the current state);
3. evaluates the transitions: the ones from the current state, plus the *any* transitions, and keeps the one with the highest priority (on a tie, the first declared);
4. switches state if one was chosen;
5. calls the current state's `on_update(dt)`;
6. applies the axes;
7. clears the triggers.

An *any* transition to the state you are already in is ignored, so it does not restart the state every tick.

When a state is left, its `on_exit` runs, and by default **all axes are set back to 0** (`stop_axes_on_exit`). That is what prevents an enemy from walking on after it changes its mind. Turn it off in the state options if you want an axis to survive a change.

## Step 3: create the brain as a component

A component is the natural owner. It runs inside the actor's lifecycle, and its `Tick` already respects the game state. The brain is created in `BeginPlay`, when the owner exists:

```cpp
#include <cmath>
#include <memory>

struct EnemyBrain : lynx::Component
{
    float see_radius = 10.f;
    float speed = 3.f;

protected:
    void BeginPlay() override
    {
        ai_ = std::make_unique<lynx::AIBehavior>(GetOwner());

        // An axis is a way to act on the actor. Here it only stores a direction;
        // the movement itself is applied in Tick.
        ai_->register_axis("move_x", [this](float v) { move_dir_ = v; });

        ai_->add_state("patrol", {
            .on_enter = [this] { ai_->set_axis("move_x", 1.f); },
        });

        ai_->add_state("chase", {
            .on_update = [this](float) { ai_->set_axis("move_x", DirectionToTarget()); },
        });

        ai_->add_state("flee", {
            .on_update = [this](float) { ai_->set_axis("move_x", -DirectionToTarget()); },
        });

        // patrol <-> chase, depending on a parameter we set ourselves
        ai_->add_transition("patrol", "chase", ai_->is_true("sees_target"));
        ai_->add_transition("chase", "patrol", ai_->is_false("sees_target"));

        // from anywhere: run away when hurt, with a high priority
        ai_->add_any_transition("flee", ai_->less("hp", 20.f), 10);

        // and calm down after a few seconds
        ai_->add_transition("flee", "patrol", ai_->state_time_greater(3.f));

        ai_->set_default_state("patrol");
    }

    void Tick(float dt) override
    {
        Sense();
        ai_->Tick(dt);

        GetOwner()->transform.location.x += move_dir_ * speed * dt;
    }

private:
    void Sense()
    {
        target_ = lynx::Engine::Get()->GetCurrentLevel()->GetActorFromID("Player");
        float distance = 1e9f;
        if (target_)
            distance = std::abs(target_->transform.location.x - GetOwner()->transform.location.x);

        ai_->set_bool("sees_target", target_ && distance < see_radius);
    }

    float DirectionToTarget() const
    {
        if (!target_) return 0.f;
        return target_->transform.location.x < GetOwner()->transform.location.x ? -1.f : 1.f;
    }

    std::unique_ptr<lynx::AIBehavior> ai_;
    lynx::Actor* target_ = nullptr;
    float move_dir_ = 0.f;
};

actor->AddComponent<EnemyBrain>();
```

Notes on this code:

- States are defined with designated initializers (`.on_enter = ...`), which needs C++20.
- The AI **decides** (it sets axes and parameters); the component **acts** (it moves the actor). Keeping those two apart makes the brain easy to test and to reuse with another body.
- `hp` is a parameter too: update it with `ai_->set_float("hp", current_hp)` whenever your health changes.

## Step 4: the ready-made conditions

You use them on the instance:

- `is_true(name)` and `is_false(name)`
- `greater(name, value)` and `less(name, value)`
- `triggered(name)`
- `state_time_greater(seconds)`: true when the current state has been active for more than that long

A condition is a `std::function<bool(const AIBehavior&)>`, so you can also write your own:

```cpp
ai_->add_transition("chase", "attack", [this](const lynx::AIBehavior&)
{
    return target_ && Distance() < attack_range;
});
```

## Step 5: actions and triggers

An **action** is a named event you can run on demand. A **trigger** is a one-tick signal you can use as a transition condition:

```cpp
ai_->register_action("attack", [this] { Strike(); });

ai_->add_state("attack", {
    .on_enter = [this] { ai_->do_action("attack"); },
});

ai_->add_transition("attack", "chase", ai_->state_time_greater(0.6f));

// from your gameplay code, for example when an enemy is hit:
ai_->set_trigger("alerted");
ai_->add_any_transition("chase", ai_->triggered("alerted"));
```

`do_action` returns `false` if the action does not exist. Setting an axis that was never registered is silently ignored, so check the names when something does not move.

`force_state(name)` switches at once and ignores the rules, which is useful for scripted moments such as a cutscene.

## Step 6: state machine or behavior tree?

Both are good, and they answer different needs.

Choose an **`AIBehavior` state machine** when:

- the behavior is a few distinct modes and transitions between them;
- you want everything in C++, close to your gameplay code;
- you need to know at all times *which* single state the AI is in.

Choose a **behavior tree** when:

- the AI is a priority list of things to try, with fallbacks;
- you want designers to edit it visually, with live debugging;
- you want to reuse nodes (`MoveTo`, `Wait`, services) and write the rest in JavaScript.

Nothing stops you from mixing them: the animation of the same enemy is already a state machine of its own, as explained in [How the animation system works](tutorials.html?tutorial=how-the-animation-system-works).

## Going further

- [How behavior trees work](tutorials.html?tutorial=how-behavior-trees-work) shows the other approach.
- [Your first C++ Actor](tutorials.html?tutorial=your-first-cpp-actor) covers building the game module that contains the component.
