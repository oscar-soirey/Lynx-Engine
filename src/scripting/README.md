# Scripting JS (QuickJS-ng)

Modèle léger : un script n'est pas une classe, c'est un comportement qu'on attache
à un `Actor`. Le même fichier peut être attaché à plusieurs acteurs ; chaque attache
a son propre `parent` et ses propres variables.

```js
let speed = 3.0;               // propre à chaque acteur

function BeginPlay() {         // lancement du jeu (ou spawn si le jeu tourne)
    parent.transform.position.x = 5;
}
function Update(dt) {          // chaque tick de jeu
    parent.transform.position.x += dt * speed;
}
function EndPlay() { }         // arrêt du jeu, retrait du script ou destruction de l'acteur
```

Toutes les fonctions sont optionnelles. Le code au niveau du fichier s'exécute
une fois par acteur, au moment où le script est attaché.

## Attacher un script

- Niveau XML : `<Player object_id_="p1" scripts="scripts/Player.js;scripts/Blink.js"/>`
- C++ : `actor->AddScript("scripts/Player.js");` ou `actor->AddComponent<lynx::ScriptComponent>()`
- JS : `parent.addScript("scripts/Blink.js")`

Les chemins sont relatifs aux assets (`fs::ReadBinary`). La propriété `scripts` est
sauvegardée par `Level::SaveToFile`. Un script introuvable ou en erreur reste dans
la liste (il est réessayé par `lynx::ReloadScripts()`).

## API `parent` / Actor

| Membre | Description |
|---|---|
| `id`, `className`, `valid`, `entity` | id (modifiable), classe C++, faux si détruit, id EnTT |
| `transform.position` / `.location`, `.rotation`, `.scale` | références vivantes : `x`, `y`, `z`, `set(x,y,z)`, `clone()` |
| `transform = {position, rotation, scale}` | affectation partielle possible |
| `position` | raccourci de `transform.position` |
| `velocity`, `angularVelocity`, `damping` | VelocityComponent (créé à l'écriture) |
| `lifetime` | secondes avant destruction auto (`null` pour retirer) |
| `getProperty(n)`, `setProperty(n, v)`, `hasProperty(n)` | propriétés `HPROPERTY` du C++ |
| `addTag`, `removeTag`, `hasTag`, `tags` | TagsComponent |
| `addScript`, `removeScript`, `hasScript`, `scripts` | scripts de l'acteur |
| `call(name, ...args)` | appelle une fonction dans les scripts de cet acteur |
| `destroy()` | destruction différée (fin de frame) |

Un acteur a un seul objet JS : `Level.find("a") === Level.find("a")`, et on peut y
ranger des données partagées entre ses scripts (`parent.hp = 3`). Utiliser un acteur
détruit lève une `ReferenceError` au lieu de crasher.

## Globaux

- `print(...)`, `console.log / warn / error`
- `vec3(x, y, z)` (ou `vec3(s)`) : objet `{x, y, z}`
- `Level.spawn(className, {position, rotation, scale} | {x,y,z})`, `Level.find(id)`,
  `Level.findWithTag(tag)`, `Level.all()`, `Level.count(className)`, `Level.destroy(actor)`
- `Input.pressed(action)`, `Input.held(action)`, `Input.released(action)`, `Input.axis(axis)`
- `Engine.getTimeDilation()`, `Engine.setTimeDilation(v[, durée])`, `Engine.isPlaying()`
- `globalThis` est partagé par tous les scripts. Les promesses et `async` fonctionnent.

## Depuis le C++

```cpp
if (auto* sc = actor->GetComponent<lynx::ScriptComponent>())
    sc->CallFunction("TakeDamage", {10.0});

lynx::ReloadScripts();                 // hot reload
lynx::ExecuteScript("print(Level.all().length)");
```

## Composants

Tous les composants héritent de `lynx::Component` (`gameplay/Component.h`).
Pour en ajouter un, dans le moteur ou dans le jeu, il suffit d'en hériter :

```cpp
struct HealthComponent : lynx::Component
{
    float hp;
    explicit HealthComponent(float start) : hp(start) {}

protected:
    void BeginPlay() override { }
    void Tick(float dt) override { }
    void EndPlay() override { }
};

auto& h = actor->AddComponent<HealthComponent>(100.f);
```

Méthodes appelées par le moteur (toutes optionnelles) :
`OnAttach()` (après l'ajout), `BeginPlay()`, `Update(dt)` (chaque frame, même hors jeu),
`Tick(dt)` (chaque tick de jeu, si `tick_enabled`), `EndPlay()`.
Un composant ajouté pendant le jeu reçoit `BeginPlay` au tick suivant.

API sur `Actor` : `AddComponent<T>(args...)` (retourne l'existant s'il y en a déjà un),
`GetComponent<T>()` (nullptr sinon), `HasComponent<T>()`, `RemoveComponent<T>()`
(appelle EndPlay, sans danger même pendant un Tick), `GetComponents()`.

Composants fournis : `TagsComponent`, `VelocityComponent`, `LifetimeComponent`,
`ScriptComponent` (`gameplay/Components.h`).
