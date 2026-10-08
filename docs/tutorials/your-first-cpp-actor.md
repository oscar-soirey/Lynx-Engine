You can make a whole game with the editor and JavaScript. C++ is the extension layer, for when you want custom Actors, Components or native systems. This tutorial adds a small native Actor to a game module and uses it from the editor and from JavaScript.

## What you will learn

- how a game project is built as a separate DLL
- how to write an Actor and expose properties to the editor and scripts
- how to register the class in the factory
- how to reload the game without restarting the editor

## Step 1: set up the build

A game project is built as a shared library that the editor and the runtime load. With CMake:

```cmake
find_package(Lynx CONFIG)

add_library(MyGame SHARED
  src/Coin.cpp
)

target_link_libraries(MyGame PRIVATE Lynx::Lynx Lynx::HRL)
```

Use the same compiler, toolchain and engine version as the editor you run, otherwise the DLL will not load correctly.

## Step 2: write an Actor

Native classes derive from `lynx::Actor` (or from a class built on it, like `Pawn` in the docs). Properties and functions are **not** visible by default: Lynx uses explicit reflection, and you register what you want to expose in the constructor.

```cpp
// src/Coin.h
class Coin : public lynx::Actor
{
public:
    float value_ = 10.f;

    void Collect();

    Coin()
    {
        HPROPERTY(value_, lynx::Exposed);
        HFUNCTION(Collect);
    }
};
```

- `HPROPERTY` exposes a value to the **Details** panel, to level serialization and to JavaScript.
- `HFUNCTION` registers a callable function in the same reflection system.
- Supported property types are `int`, `float`, `bool`, `std::string`, `vec2`, `vec3`, `vec4` and `transform`.

## Step 3: register the class

The factory needs to know your class by name. Register it in the module entry:

```cpp
LYNX_LINK_MODULE(
    LYNX_MODULE_REGISTER(Coin);
);
```

When the DLL loads, the class is added to the factory. When it unloads, the engine removes the constructors that belong to that DLL, so nothing keeps a pointer into code that no longer exists.

## Step 4: build and place it

Build the project, then in the editor use **Reload Game** to load the new DLL without restarting. `Coin` should now be available when you place Actors, and in level files.

Select a Coin in the level: `value_` appears in **Details** and can be edited like any other property.

## Step 5: use it from JavaScript

Spawn it, or attach a script to a placed Coin. Reflected properties are readable and writable by name:

```js
// scripts/CoinLogic.js
function BeginPlay() {
  if (parent.hasProperty("value_")) {
    const v = parent.getProperty("value_");
    console.log("Coin worth", v);
    parent.setProperty("value_", v * 2);
  }
}
```

Spawning from another script works with the registered class name:

```js
Level.spawn("Coin", vec3(4, 1, 0));
```

`setProperty()` throws a type error if the value does not match the property type, so a float property needs a number.

## Step 6: add a Component

Reusable behavior goes in a Component. An Actor can have one component of a given type.

```cpp
struct Health : lynx::Component
{
    float hp = 100.f;

protected:
    void Tick(float dt) override
    {
        if (hp <= 0.f)
            GetCurrentLevel()->DestroyActor(GetOwner());
    }
};

actor->AddComponent<Health>();
```

Callbacks are optional: `OnAttach()`, `BeginPlay()`, `Update(dt)`, `Tick(dt)` and `EndPlay()`. `Update` also runs outside gameplay, while `Tick` is gameplay simulation and respects `tick_enabled`.

## Going further

- Keep the iteration loop short: write gameplay in JavaScript, and move to C++ only what needs it.
- The [Architecture](architecture.html) page explains the engine, Level, Actor and ECS layers.
