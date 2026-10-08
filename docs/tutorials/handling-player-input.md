In Lynx, scripts never read raw keys. They ask for named **actions** and **axes** that you define in the project. This tutorial sets up a movement axis and a jump action, then uses them in a script.

## What you will learn

- the difference between an action and an axis
- how to edit your mappings in Input Settings
- how to read input from JavaScript

## Step 1: actions and axes

- An **action** is a button-like event: `jump`, `attack`, `interact`.
- An **axis** is a number: `move_x` goes from -1 (left) to 1 (right), and 0 when nothing is pressed.

Both are stored in `input.json` at the root of your `assets/` folder, under *Action Mappings* and *Axis Mappings*. They can use the keyboard, the mouse and gamepads.

## Step 2: open Input Settings

You do not need to edit `input.json` by hand. Open **Input Settings** in the editor and create:

- an axis named `move_x`, with a key for each direction (for example `KEY_A` for negative and `KEY_D` for positive)
- an action named `jump`, bound to `KEY_SPACE`

Changes are saved and applied immediately, even while the game is running. Named key codes such as `KEY_SPACE` and `KEY_W` are supported, and older numeric codes are still accepted.

## Step 3: read the axis

Create `scripts/Mover.js`:

```js
function Update(dt) {
  const x = Input.axis("move_x");
  parent.position.x += x * 8 * dt;
}
```

Attach it to an Actor through its **scripts** property and press Play. The Actor now moves left and right at 8 units per second.

## Step 4: react to an action

There are three functions for actions, and each answers a different question:

- `Input.pressed(name)` is true on the frame the action went down.
- `Input.held(name)` is true while the action is down.
- `Input.released(name)` is true on the frame the action went up.

Use `pressed` for one-shot events such as a jump:

```js
function Update(dt) {
  parent.position.x += Input.axis("move_x") * 8 * dt;

  if (Input.pressed("jump"))
    parent.velocity.y = 12;
}
```

Use `held` for something continuous, like charging a shot or sprinting.

## Step 5: why names matter

An action is resolved by name each time you call it. If you rename a mapping in Input Settings but not in your script, the script will quietly stop reacting. When an input does nothing, check the spelling first.

## Going further

- Add a gamepad stick to `move_x` in Input Settings: your script does not change.
- [Spawning and finding Actors](tutorials.html?tutorial=spawning-and-finding-actors) shows how to shoot something when `attack` is pressed.
