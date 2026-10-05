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

## Classes

En plus des scripts attachés, on peut écrire des classes d'acteurs :

```js
// assets/classes/Enemy.js
class Enemy extends Pawn {                     // Actor, ou n'importe quelle classe C++ du jeu
    static properties = {                      // éditables (Details), sauvegardées avec le niveau
        hp: 100,
        speed: 2.5,
        label: "grunt",
        target: { x: 0, y: 0, z: 0 },          // vec3 ({x,y} : vec2, {x,y,z,w} : vec4)
        armor: { value: 3, type: "int" },      // type forcé : int float bool string vec2 vec3 vec4
    };

    constructor() {
        super();
        this.hits = 0;                         // champ JS simple (non sauvegardé)
    }

    BeginPlay() {
        this.sprite = this.addComponent("StaticSprite", { texture: "enemy.png" });
        this.move_speed_ = 6;                  // HPROPERTY du C++ (même protected)
        this.Jump();                           // HFUNCTION du C++ (même protected)
    }

    Update(dt) { this.position.x += this.speed * dt; }
    EndPlay() { }

    TakeDamage(n) { this.hp -= n; }
}

class Boss extends Enemy { /* ... */ }        // héritage entre classes JS, autre fichier possible
```

- **Où** : n'importe où dans `assets/` (tous les dossiers et sous-dossiers ; `classes/` n'est
  qu'une convention). Avant chaque niveau, chaque `.js` qui déclare une `class Nom extends ...`
  est chargé ; les scripts attachés (sans classe) ne sont pas touchés. Seules les classes qui
  héritent d'`Actor` sont enregistrées. L'ordre des fichiers n'a pas d'importance.
- **Où elles apparaissent** : dans la factory, comme une classe C++. Elles sont donc dans
  *Place Actors* de l'éditeur, dans les niveaux (`<Enemy object_id_="e1" hp="40"/>`),
  et se créent par `Level.spawn("Enemy")` ou `new Enemy()` en JS (ajouté au niveau).
- **`this`** : c'est l'acteur, avec toute l'API (`position`, `transform`, `addComponent`,
  `addTag`, `destroy`...) ; c'est aussi le `parent` des scripts attachés et l'objet
  retourné par `Level.find`.
- **Cycle de vie** : `BeginPlay()`, `Update(dt)` (chaque tick de jeu), `EndPlay()`, comme
  les scripts. Le code C++ de la classe parente (Tick du Pawn...) tourne aussi.
  `OnBeginOverlap(other)`, `call("Nom")` et `lynx::CallScriptFunction` atteignent aussi
  les méthodes de la classe.
- **Rechargement** : `lynx::ReloadScripts()` relit les classes ; les acteurs existants
  prennent les nouvelles méthodes.
- **Nom** : une classe JS ne peut pas porter le nom d'une classe C++ de la factory
  (dans Blocky, `Player` existe déjà en C++).
- La syntaxe est celle de JavaScript : `class Player extends Actor`
  (pas `class Player : extends Actor`).

### Membres C++ visibles en JS

Le C++ n'a pas de réflexion automatique : un membre est visible en JS s'il est déclaré
dans le constructeur de la classe C++ (public, protected ou private) :

```cpp
Pawn::Pawn()
{
    HPROPERTY(move_speed_, lynx::Exposed);   // this.move_speed_ (lecture / écriture)
    HFUNCTION(Jump);                         // this.Jump()
    HFUNCTION(Move);                         // this.Move(1)
}
```

Types : `int`, `float`, `bool`, `std::string`, `vec2`, `vec3`, `vec4`, `transform`
(et les autres nombres, convertis). Fonction surchargée :
`RegisterFunction("Move", static_cast<void (Pawn::*)(float)>(&Pawn::Move));`.
Si une méthode JS porte le même nom, elle gagne ; la version C++ reste accessible par
`this.callNative("Jump")` (et les propriétés par `getProperty` / `setProperty`).
Ces membres sont aussi visibles sur tout acteur C++ : `Level.find("Pawn").move_speed_`.

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
- Joueurs : `Engine.createPlayer()`, `Engine.destroyPlayer(p)`, `Engine.getPlayer(i)`,
  `Engine.players`, `Engine.playerCount`, `Engine.defaultPlayer`, `Engine.renderSize`
- Widgets : `UI.create(path, player, classe)`, `UI.destroy(w)`, `UI.all()`, `UI.classes()`,
  `UI.getDPIReference()`, `UI.setDPIReference(v)` ; `UserWidget` (classe de base)
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
`ScriptComponent` (`gameplay/Components.h`), et les composants de scène ci-dessous.

## Composants de scène

| C++ | JS | Fichier |
|---|---|---|
| `StaticSpriteComponent` | `"StaticSprite"` | `gameplay/SpriteComponents.h` |
| `AnimationSpriteComponent` | `"AnimationSprite"` | `gameplay/SpriteComponents.h` |
| `CameraComponent` | `"Camera"` | `gameplay/CameraComponent.h` |
| `BoxColliderComponent` | `"BoxCollider"` | `gameplay/BoxColliderComponent.h` |
| `SoundSourceComponent` | `"SoundSource"` | `gameplay/SoundSourceComponent.h` |
| `LightComponent` (`PointLightComponent`, `SpotLightComponent`, `DirectionalLightComponent`, `SkyLightComponent`) | `"Light"` (`type: "point" / "spot" / "directional" / "sky"`) | `gameplay/LightComponent.h` |
| `VelocityComponent`, `LifetimeComponent` | `"Velocity"`, `"Lifetime"` | `gameplay/Components.h` |

En C++, ce sont des champs publics et des méthodes ; les champs sont appliqués à la
frame suivante (ou tout de suite avec `Refresh()` pour les sprites) :

```cpp
auto& sprite = actor->AddComponent<lynx::StaticSpriteComponent>();
sprite.texture = "hero.png";
sprite.size = {2.f, 3.f};

auto& box = actor->AddComponent<lynx::BoxColliderComponent>();
box.on_begin_overlap = [](lynx::BoxColliderComponent& other) { /* ... */ };
box.MoveAndCollide({vx * dt, vy * dt, 0.f});
```

En JS, mêmes noms en camelCase :

```js
const sprite = parent.addComponent("StaticSprite", { texture: "hero.png", size: {x: 2, y: 3} });
sprite.visible = false;
parent.getComponent("SoundSource")?.play();
parent.hasComponent("Camera");
parent.removeComponent("Light");
parent.components;            // ["StaticSprite", "SoundSource", ...]
```

`addComponent(nom, options)` retourne le composant existant s'il y en a déjà un, puis
applique `options` ; une propriété inconnue lève une erreur (faute de frappe).
Un objet composant ne garde que (acteur, type) : après `removeComponent` ou la
destruction de l'acteur, l'utiliser lève une `ReferenceError` (`comp.valid` vaut
`false`). Les vecteurs sont des copies : `sprite.offset = vec3(0, 2, 0)`, pas
`sprite.offset.y = 2`.

Commun à tous : `owner` (acteur), `type`, `valid`, `tickEnabled`, `hasBegunPlay`.

### StaticSprite / AnimationSprite

Quad texturé centré sur l'acteur. Il suit sa position et sa scale (scale X négative =
retourné) et se sélectionne dans l'éditeur en cliquant dessus.

| Propriété | |
|---|---|
| `size` `{x, y}` | taille monde (avant la scale de l'acteur) |
| `offset` `{x, y, z}` | décalage (suit le retournement) |
| `visible`, `flipX`, `flipY` | |
| `texture`, `region` `{x: u0, y: v0, z: u1, w: v1}` | StaticSprite |

**AnimationSprite**, planche horizontale de `frameCount` images. Deux modes :

*Une seule animation* (`mode = "single"`) :

```js
const anim = parent.addComponent("AnimationSprite");
anim.setAnimation("explosion.png", 8, 0.05, false);   // texture, frames, frameTime, loop
anim.onFrame(4, () => parent.getComponent("SoundSource").play());
anim.onFinished(() => parent.destroy());
anim.pause(); anim.play(); anim.stop(); anim.restart();
anim.frame; anim.playing; anim.finished;
```

Propriétés : `texture`, `frameCount`, `frameTime`, `loop`, `autoPlay` (modifiables à tout moment).

*Pilotée par une machine à états* (`mode = "stateMachine"`, voir `AnimationSystem.h`) :

```js
anim.addAnimation("idle", "hero_idle.png", 4);               // nom, texture, frames, loop, frameTime
anim.addAnimation("run", "hero_run.png", 8, true, 0.08);
anim.addAnimation("hit", "hero_hit.png", 3, false);
anim.addBlendSpace("move", [[0, "idle"], [6, "run"]]);       // ou [{position, animation}]

anim.addState("move", "move", { variable: "speed" });         // le blend space suit "speed"
anim.addState("hit", "hit", { interruptible: false, onEnter: () => {}, onExit: () => {} });
anim.setDefaultState("move");

anim.addAnyTransition("hit", { trigger: "hurt" }, 10);         // (to, condition, priorite)
anim.addTransition("hit", "move", s => s.finished, 0, true);  // (from, to, condition, priorite, attendreFin)

anim.setFloat("speed", Math.abs(parent.velocity.x));
anim.trigger("hurt");
anim.currentState;
```

Conditions : une fonction `(sprite) => bool`, ou `{param: "speed", op: ">", value: 0.1}`
(`> >= < <= == !=`), `{isTrue: "airborne"}`, `{isFalse: "airborne"}`, `{trigger: "hurt"}`.
Autres : `setBool`, `setInt`, `getFloat`, `getBool`, `forceState`, `stopAll`, `resumeAll`,
`onAnimationFrame(anim, frame, fn)`, `onAnimationFinished(anim, fn)`.
Options d'état : `interruptible`, `restartOnEnter`, `next`, `variable`, `onEnter`, `onExit`.

En C++ : `AddAnimation`, `AddBlendSpace` / `AddBlendSample`, `AddState`, `SetDefaultState`,
`SetFloat`... et `GetAnimManager()` pour tout le reste (`add_transition`, conditions...).

Les animations avancent pendant le jeu, au rythme du temps de jeu (time dilation).

### Camera

```js
const cam = parent.addComponent("Camera", { offset: vec3(0, 2, 80), followSpeed: 3 });
cam.activate();      // devient la camera de la vue (auto au lancement si autoActivate)
```

`offset`, `rotation` (degrés, défaut `(0, -90, 0)`), `fov`, `near`, `far`, `followSpeed`
(0 = colle à l'acteur), `useCameraShake`, `autoActivate`, `active`, `snap()`.
À l'arrêt du jeu, l'éditeur reprend sa caméra. Si le jeu déplace déjà sa caméra avec
`LynxGame_UpdateGameplayCamera`, utiliser l'un ou l'autre.

### BoxCollider

Boîte 2D alignée sur les axes. Deux boîtes interagissent si `a.layer & b.mask` et
`b.layer & a.mask`. Une boîte `trigger` ne bloque jamais.

```js
const box = parent.addComponent("BoxCollider", { size: {x: 1, y: 2}, debugDraw: true });
const r = box.moveAndCollide({x: vx * dt, y: vy * dt, z: 0});   // s'arrete contre voxels et boites
if (r.blockedY) vy = 0;
box.onBeginOverlap(other => print("touche", other.id));
box.overlapping();            // acteurs
box.overlapsVoxels();
box.voxelFlags = ["ROCK"];    // voxels bloquants (defaut : voxels pleins)

function OnBeginOverlap(other) { }   // appele aussi dans les scripts de l'acteur
function OnEndOverlap(other) { }
```

`size`, `offset`, `trigger`, `layer`, `mask`, `collideWithVoxels`, `voxelFlags`, `debugDraw`,
`isOverlapping(acteur)`, `onEndOverlap(fn)`. Pas de physique : `moveAndCollide` déplace
l'acteur en s'arrêtant au contact (X puis Y), sans traverser les murs fins.

### SoundSource

```js
const s = parent.addComponent("SoundSource", { sound: "footstep1.wav", volume: 0.8, pitchVariation: 0.1 });
s.play(); s.stop(); s.playing; s.playAt(vec3(0, 0, 0));
```

`sound`, `spatial` (3D qui suit l'acteur / 2D), `loop`, `playOnBegin`, `volume`, `pitch`,
`volumeVariation`, `pitchVariation`, `referenceDistance`, `maxDistance`, `rolloff`.

### Light

`type` (`"point"` / `"sky"`), `color`, `intensity`, `offset`, `enabled`.

## Acteurs du moteur

`Actor`, `PointLightActor`, `SpotLightActor`, `DirectionalLightActor`, `SkyLightActor`,
`SpriteActor` et `SoundActor` sont enregistrés par le moteur (`gameplay/EngineActors.h`) :
`Level.spawn("PointLightActor")`, et une classe JS peut en hériter
(`class Torch extends PointLightActor { ... }`).

## Joueurs (PlayerController)

Chaque joueur a son viewport (split-screen automatique). Le joueur 0 existe toujours.

```js
const p2 = Engine.createPlayer();            // détruit à la fin du jeu s'il est créé pendant
p2.possess(Level.spawn("Hero"));             // l'ancien joueur de Hero le relâche
p2.possessed;                                // acteur (ou null) ; alias : pawn
p2.unpossess();
p2.setViewTarget(Level.find("cam"));         // sa CameraComponent devient la vue de p2
p2.setViewportSize(0, 0, 0.5, 1);            // fixe (normalisé) ; resetViewportSize() : auto
p2.index; p2.id; p2.isDefault; p2.valid; p2.viewportRect;   // {x, y, width, height}
p2.createWidget("ui/Hud.widget");            // = UI.create(path, p2)

const hero = Level.find("h1");
hero.controller;                             // PlayerController ou null
hero.isPossessed;
hero.autoPossessPlayer = 0;                  // = propriété auto_possess_player (-1 : non)
hero.getComponent("Camera").activateFor(p2);
```

Dans une classe d'acteur (ou un script attaché) :

```js
class Hero extends Actor {
    constructor() { super(); this.autoPossessPlayer = 0; }   // possédé au lancement du jeu
    OnPossessed(player) { }
    OnUnpossessed(player) { }
    ProcessInput(player) {           // chaque frame de jeu, seulement quand il est possédé
        if (Input.pressed("Jump")) this.Jump();
    }
}
```

## Widgets (UI)

Les `.widget` se font dans le Widget Editor (voir `widgets/README.md`).

```js
const hud = UI.create("ui/Hud.widget", Engine.defaultPlayer);   // joueur : optionnel
hud.addToViewport(10);                       // z-order ; removeFromParent() : retiré, réutilisable
hud.find("Health").percent = 0.4;            // champs en camelCase, vus à la frame suivante
hud.find("Title").set({ text: "Niveau 2", fontSize: 32, color: "#ffcc00" });
hud.find("Pause").onClicked(() => Engine.setTimeDilation(0));
hud.find("Volume").onValueChanged(v => print(v));
hud.find("Mute").onCheckStateChanged(on => print(on));
const id = btn.on("hovered", () => {}); btn.off("hovered", id);
UI.destroy(hud);                             // ou hud.destroy()
```

- **Champs** : ceux du Widget Editor, en camelCase (`text`, `fontSize`, `percent`,
  `renderOpacity`, `isEnabled`...). Enums en texte : `visibility = "Collapsed"`
  (`Visible`, `Collapsed`, `Hidden`, `HitTestInvisible`, `SelfHitTestInvisible`),
  `hAlign`, `vAlign`, `sizeRule`, `orientation`. Couleurs : `{r, g, b, a}`, `[r, g, b, a]`
  ou `"#rrggbb(aa)"`. Marges : `{left, top, right, bottom}` ou `{x, y, z, w}`.
- **Arbre** : `name`, `className`, `parent`, `children`, `childCount`, `root`, `find(nom)`,
  `getChildAt(i)`, `userWidget`, `visible` (raccourci Visible / Collapsed), `hovered`,
  `geometry`, `desiredSize`, `inViewport`, `owningPlayer` (modifiable), `valid`.
- **Construire** : `panel.addChild("Button", { name, text, slot: { sizeRule: "Fill" } })`,
  `insertChildAt(i, classe, options)`, `addChild("ui/Barre.widget")` (un autre widget),
  `removeChild(w)`, `clearChildren()`, `w.removeFromParent()` (détruit un widget d'un arbre).
- **Slot** : `w.slot.offsets`, `.anchorMin`, `.padding`... et pour un CanvasPanel
  `w.slot.setPosition(x, y).setSize(w, h).setAnchors(minX, minY, maxX, maxY).setAlignment(x, y)`.
- **Événements** : `onClicked`, `onPressed`, `onReleased`, `onHovered`, `onUnhovered` (Button),
  `onValueChanged` (Slider), `onCheckStateChanged` (CheckBox). Appelés au début de la frame
  suivante : on peut détruire n'importe quel widget dedans.
- Un widget détruit : `valid` vaut `false`, l'utiliser lève une `ReferenceError`.

### Classe de widget (Widget Blueprint en JS)

```js
// n'importe où dans assets/, comme les classes d'acteurs
class MainMenu extends UserWidget {
    Construct() {                            // l'arbre du .widget existe
        this.find("Play").onClicked(() => this.removeFromParent());
    }
    Tick(dt) { }                             // chaque frame tant qu'il est à l'écran
    Destruct() { }
}
```

Dans le Widget Editor, mettre `MainMenu` dans *Class* (bouton *Copy JS class* : squelette
avec les événements des widgets nommés) : `UI.create("ui/MainMenu.widget")` et
`lynx::CreateWidget` en C++ créent alors un `MainMenu`. Sinon : `UI.create(path, player, MainMenu)`.
`new MainMenu()` n'est pas possible (pas de constructeur : initialiser dans `Construct`).
