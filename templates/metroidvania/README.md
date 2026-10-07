# {{PROJECT_TITLE}} : Metroidvania (Lynx Hollow)

A complete little Hollow Knight-like game, **all JavaScript**, in a destructible
voxel cave. Press **Play** : the title screen waits for JUMP.

## Controls

| Keyboard | Gamepad | |
|---|---|---|
| A / D, arrows | left stick | move |
| SPACE / Z (hold : higher) | A | jump - double jump (wings) - wall jump (claw) |
| J / X / left click | X | attack, 3-hit combo ; W + J up ; S + J in the air : down (pogo) |
| K / C / SHIFT | right trigger | dash (once in the air, refilled by walls, pogo, landing) |
| L / V / right click | left bumper | parry : stuns the knights, sends the spit back |
| F (hold) | Y | focus : 33 SOUL -> 1 mask (soul comes from hitting enemies) |
| B / Q | B | bomb (S : drop it) : breaks ROCK |
| E | right bumper | dirt block (S + E : step up ; in the air : a bridge) |
| R | left trigger | grappling hook (W : straight up) |
| W | d-pad up | rest on a bench (heal + respawn point) |
| ESC / P | start | pause |

## The game

- **Dig** : the nail breaks DIRT, MOSS, WOOD and SAND (your dirt pouch fills up) ;
  ROCK needs bombs ; BEDROCK, SPIKES and the SEALS never break.
- **Abilities** : 5 shrines (dash, mantis claw, wings, grappling hook, bombs). The
  Warden sleeps under a rock plug in the Lower Halls : bombs open the way.
- **Masks** : 3 mask shards are hidden in pockets of dirt.
- **Hazards** : spikes and acid send you back to the last safe ground.
- **Death** : back to the last bench, full health.

## What it shows of the engine

| | |
|---|---|
| `classes/Wanderer.js` | Humanoid player : coyote time, jump buffer, variable jump, wall slide / jump, dash, combo, pogo, parry, focus, hook, blocks, hit-stop (`Engine.setTimeDilation`), camera shake, look-ahead camera, HUD |
| `anim/Wanderer.animgraph`, `anim/Warden.animgraph` | Anim Graphs : state machines, Any State transitions (triggers / bools), notifies (`OnFootstep`, `OnStomp`) |
| `ai/*.bt` | Behavior Trees : `FindNearest` / `DistanceTo` services, Blackboard / CallFunction / Cooldown decorators with aborts, tasks written as JS methods returning `BT.Running` |
| `classes/Enemies.js` | Crawler, Hopper, Spitter, Shield Knight, Burrower (digs through the terrain) |
| `classes/Warden.js` | the boss : 2 phases, slam + shockwaves, charge, falling rocks that break the ceiling, burrowing |
| `classes/World.js` | benches, shrines, hints, the boss arena (sealed with voxels : `Voxels.fillRect`), the pause menu (`UserWidget` class) |
| `classes/Interfaces.js` | `Collector`, `Parryable`, `NailTarget`, `GameEvents` : classes talk through interfaces |
| `ui/*.widget` | HUD (masks, soul, geo, abilities, boss bar, fade), title, pause menu |
| `voxels.json` | flags SOFT / HARD / CRYSTAL, contact events (spikes, acid), sand physics, emissive crystals |

Tip : in Details, the Wanderer has `has_dash`, `has_claw`... (start with the
abilities) and `skip_title`, to test a part of the map quickly.
