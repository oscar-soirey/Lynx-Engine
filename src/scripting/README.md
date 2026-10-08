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

## Interfaces (communication entre classes)

On ne modifie pas directement une variable d'une autre classe (`player.coin += 1` depuis `Coin.js`).
On lui envoie un **message d'interface**, comme les interfaces d'Unreal : celui qui envoie ne connaît
pas la classe de celui qui reçoit, et si le receveur n'implémente pas l'interface, il ne se passe rien.

```js
// Collectible.js : une interface. Les méthodes déclarent les fonctions ;
// leur corps est l'implémentation par défaut (souvent vide).
class Collectible extends Interface {
    OnCollected(item, amount) {}
}

// Player.js : implémente l'interface.
class Player extends Humanoid {
    static interfaces = [Collectible, "Damageable"];   // classe ou nom
    static properties = { coins: 0 };
    OnCollected(item, amount) { this.coins += amount; }
    TakeDamage(amount, instigator) { print("aïe", amount); }
}

// Coin.js : ne connaît pas Player.
class Coin extends Actor {
    OnBeginOverlap(other) {
        if (!other.implements(Collectible)) return;      // un ennemi, un mur...
        other.send(Collectible, "OnCollected", this, 1);
        this.destroy();
    }
}
```

- Un script attaché implémente une interface avec `const interfaces = ["Collectible"];` et une
  fonction du même nom (`function OnCollected(item, amount) { ... }`).
- `actor.send(iface, fn, ...args)` : appelle la fonction (C++, classe JS, puis scripts attachés ;
  sinon l'implémentation par défaut). Retourne la dernière valeur.
- `actor.implements(iface)`, `Level.findImplementing(iface)`, `Collectible.find()`,
  `Collectible.broadcast("OnCollected", null, 0)`, `Interface.call(actor, "Collectible", "OnCollected")`,
  `Interface.list()`, `Collectible.functions()`.
- Une interface peut hériter d'une autre : `class Pickup extends Collectible { OnPicked() {} }`.
- Interfaces du moteur : `Damageable` (`TakeDamage(amount, instigator)`) et `Interactable`
  (`Interact(instigator)`, `CanInteract(instigator)`). C++ : `lynx::ApplyDamage`, `lynx::Interact`.
- C++ : `ImplementInterface("Collectible")`, `BindInterfaceFunction(...)` ou une `HFUNCTION` du même
  nom, `lynx::interfaces::Call(actor, "Collectible", "OnCollected", { this, 1 })`
  (voir `gameplay/Interface.h`).

## Voxels physiques

Les types de voxels peuvent avoir une physique (éditeur : *Voxel types > Physics*, presets Sand,
Water, Lava, Smoke, Ice, Rubber...) : poudres, liquides et gaz simulés pendant le jeu autour des
caméras. `VoxelPhysics.setEnabled(false)`, `setRadius(128)`, `setRate(30)`,
`addFocus(x, y)` (une zone de plus, à appeler chaque frame).
Événements : `static interfaces = ["VoxelEvents"]` puis `OnVoxelContact(name, type)` /
`OnVoxelContactEnd(name, type)` ; les dégâts arrivent par `Damageable.TakeDamage(amount, null)`.

## Destruction / construction de voxels (`Voxels`)

Unités monde (1 = 1 voxel). Un point : `{x, y}`, `[x, y]` ou un acteur (sa position).
Un type : son nom dans `voxels.json` (`"Stone"`) ou son id.

```js
// Explosion
const r = Voxels.destroyCircle(this, 6);
print(r.count, r.types.Gold);          // nombre détruit, par type : { Dirt: 40, Gold: 2 }
for (const c of r.cells) {}            // { x, y, type, typeName, world: {x, y} }
// r.perType : [{ type, typeName, count }], r.min / r.max / r.center

Voxels.destroyRect(center, {x: 4, y: 2});          // centre + taille (comme les colliders)
Voxels.destroyLine(from, to, 2);                   // épaisseur 2
Voxels.destroyAt(point); Voxels.destroyCell(vx, vy);

// Options (destroy*, count*) :
//   types: ["Dirt", "Grass"]   ignoreTypes: [...]   flags: "ROCK" | ["A", "B"]
//   includeIndestructible: false   events: true (on_destroyed)   cells: true (liste des cellules)
Voxels.destroyCircle(p, 5, { types: ["Dirt"], cells: false });

// Construction : cases vides seulement, sauf replace: true (+ les options ci-dessus pour choisir
// ce qui peut être remplacé). Même résultat (type = type d'avant, 0 = vide).
Voxels.fillRect({x: 10, y: 5}, {x: 2, y: 8}, "Stone");
Voxels.fillCircle(p, 3, "Water", { replace: true, types: ["Dirt"] });
Voxels.fillLine(a, b, 1, "Wood"); Voxels.fillAt(p, "Sand"); Voxels.fillCell(vx, vy, 3);

// Lecture
Voxels.countCircle(p, 4, { flags: "ROCK" });       // comme destroy, sans rien changer
Voxels.typeAt(p); Voxels.typeAtCell(vx, vy); Voxels.typeId("Stone"); Voxels.typeName(3);
Voxels.worldToCell(p);  /* {x, y} */  Voxels.cellToWorld(vx, vy);  /* centre {x, y} */
```

C++ : `lynx::voxels::DestroyCircle(center, radius, filter)`, `FillRect(...)`, `FindType("Stone")`,
`VoxelFilter`, `VoxelEditResult` (voir `core/VoxelEdit.h`).

## Requêtes physiques (`Physics`)

```js
const hit = Physics.raycast(this, target, { ignore: this, debug: true });
if (hit) {
    // hit.point, hit.normal, hit.distance, hit.fraction (0..1), hit.initialOverlap
    // hit.actor (ou null), hit.voxel : { x, y, type, typeName } (ou null)
}
Physics.raycastAll(a, b, opts);   // acteurs traversés jusqu'au premier blocage, triés par distance
Physics.lineOfSight(a, b, opts);  // true : rien entre les deux

const o = Physics.overlapCircle(center, 5, { triggers: true });
// o.actors : [Actor], o.voxels : comme Voxels.countCircle (count, types, cells...), o.any
Physics.overlapBox(center, {x: 4, y: 2}, opts);

// Options : actors: true, voxels: true, triggers: false, layers: 0xFFFFFFFF (layer des colliders),
//   ignore: acteur | [acteurs], voxelFlags: "SOLID" | [...] (traces : voxels bloquants par
//   défaut, l'eau est traversée ; overlaps : tous les voxels), types / ignoreTypes,
//   debug: true (dessine 1 frame), duration: 2 (secondes)
Physics.setDebugDraw(true);       // dessine toutes les requêtes (F4 dans l'éditeur aussi)
```

C++ : `lynx::physics::Raycast(from, to, params)`, `RaycastAll`, `LineOfSight`, `OverlapBox`,
`OverlapCircle`, `QueryParams`, `HitResult` (voir `gameplay/PhysicsQueries.h`).

## Post process

`PostProcess.set("exposure", 0.5)`, `PostProcess.set("tintColor", [1, 0.9, 0.8])`,
`PostProcess.get("bloomStrength")`, `PostProcess.reset()`, `PostProcess.params()`.
Les valeurs de départ viennent de `assets/postprocess.json` (fenêtre **Post Process** de l'éditeur).

## Particules

Systèmes de particules `.vfx` faits dans le **Particle Editor** (Content Browser > New file >
Particle System, double-clic). Effet ponctuel : `Particles.spawn("fx/explosion.vfx", position, rotation?, scale?)`
le joue une fois puis le retire (renvoie `false` si le fichier est introuvable) ; `Particles.clear()` retire
tous les effets ponctuels. Effet permanent : l'acteur `ParticleActor` (propriétés `system`, `auto_play`,
`time_scale`, `visible`), qui suit l'acteur et se recharge quand le `.vfx` est sauvegardé.

## Éclairage 2D

Éclaire le niveau vu de face (sprites, voxels, décor) avec des lumières 2D, et les voxels projettent
des ombres. À activer pour le projet : fenêtre **2D Lighting** de l'éditeur, ou
`Lighting2D.set("enabled", true)`. Réglages (sauvegardés dans `assets/lighting2d.json`) :

| Réglage | Rôle |
|---|---|
| `enabled` | éclairage 2D du projet |
| `ambientColor`, `ambientIntensity` | lumière hors des lumières 2D. Par défaut blanc / 1 : l'éclairage normal (lumières HRL, voxels émissifs) est gardé et les lumières 2D s'y ajoutent. Plus bas : plus sombre (nuit : 0.1 à 0.3) |
| `intensity` | multiplie toutes les lumières 2D |
| `shadows`, `shadowQuality` | ombres des voxels, précision des rayons |
| `edgeDepth` | voxels éclairés à l'intérieur des murs face à une lumière (bords lumineux) |
| `liquidOpacity`, `gasOpacity`, `decorOpacity` | combien l'eau, la fumée, les voxels sans collision bloquent |
| `pixelSnap` | lumière calculée par voxel (carrée, style pixel art) |
| `bands` | lumière par paliers (0 : lisse, 3 à 8 : rétro) |
| `keepBright` | les pixels émissifs restent lumineux dans le noir |

`Lighting2D.get(name)`, `Lighting2D.set(name, value)`, `Lighting2D.reset(name?)`, `Lighting2D.params()`.
Les lumières : acteur `Light2DActor` (Place Actors) ou composant `Light2D` (voir plus bas).
Au plus 16 lumières par vue (les plus proches / fortes).

## Fonctions des plugins

Les plugins (C++, voir `src/plugins/README.md`) ajoutent leurs objets JS
(`lynx::RegisterScriptFunction`) : avec les plugins livrés activés, `Dialogue`, `Story`, `Quest`
(plugin Dialogue) et `Cine`, `Sequence` (plugin CineCamera).

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
| `ColliderComponent` (ancien nom `BoxColliderComponent`) | `"Collider"` (ou `"BoxCollider"`) | `gameplay/BoxColliderComponent.h` |
| `SoundSourceComponent` | `"SoundSource"` | `gameplay/SoundSourceComponent.h` |
| `LightComponent` (`PointLightComponent`, `SpotLightComponent`, `DirectionalLightComponent`, `SkyLightComponent`) | `"Light"` (`type: "point" / "spot" / "directional" / "sky"`) | `gameplay/LightComponent.h` |
| `VelocityComponent`, `LifetimeComponent` | `"Velocity"`, `"Lifetime"` | `gameplay/Components.h` |

En C++, ce sont des champs publics et des méthodes ; les champs sont appliqués à la
frame suivante (ou tout de suite avec `Refresh()` pour les sprites) :

```cpp
auto& sprite = actor->AddComponent<lynx::StaticSpriteComponent>();
sprite.texture = "hero.png";
sprite.size = {2.f, 3.f};

auto& box = actor->AddComponent<lynx::ColliderComponent>();
box.on_begin_overlap = [](lynx::ColliderComponent& other) { /* ... */ };
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

*Anim Graph fait dans l'éditeur* (fichier `.animgraph`, voir plus bas « Anim Graph ») :

```js
const anim = this.addComponent("AnimationSprite", { graph: "anim/Hero.animgraph" });
anim.setFloat("speed", Math.abs(this.velocity.x));   // les transitions du graphe suivent
anim.setTrigger("hurt");                             // = anim.trigger("hurt")
anim.loadGraph("anim/Boss.animgraph");               // -> false si illisible
anim.loadedGraph;                                    // "anim/Boss.animgraph"
```

### Camera

```js
const cam = parent.addComponent("Camera", { offset: vec3(0, 2, 80), followSpeed: 3 });
cam.activate();      // devient la camera de la vue de son joueur
```

`offset`, `rotation` (degrés, défaut `(0, -90, 0)`), `fov`, `near`, `far`, `followSpeed`
(0 = colle à l'acteur), `followDelay` (secondes de retard, 0 = aucun), `useCameraShake`, `autoActivate`, `active`, `snap()`.
La caméra de l'acteur **possédé** par un `PlayerController` est affichée dans le viewport de
ce joueur (split-screen : chacun voit par la caméra de son acteur) ; `unpossess` : retour à
la caméra par défaut du joueur. Un acteur possédé par personne prend la vue du joueur par
défaut si `autoActivate` (sauf si ce joueur voit déjà par la caméra de son acteur).
À l'arrêt du jeu, l'éditeur reprend sa caméra.

### Collider

Boîte de collision 2D alignée sur les axes, qui sert à deux choses :

- **Chevauchement** (`trigger: true`, ou deux colliders qui se chevauchent) : les deux acteurs
  reçoivent `OnBeginOverlap(other)` puis `OnEndOverlap(other)`.
- **Blocage** (`trigger: false`, le défaut) : un collider bloquant arrête les autres colliders
  bloquants et les voxels pleins. `moveAndCollide`, et la vitesse de l'acteur (`velocity`,
  VelocityComponent) s'arrêtent au contact : l'acteur glisse le long des murs, la vitesse
  s'annule sur l'axe bloqué. Les deux acteurs reçoivent `OnHit(other, normal)` (`other` vaut
  `null` pour un voxel ; `normal` repousse l'acteur). Un collider `movable` qui se retrouve dans
  un collider bloquant (téléport, spawn) en est repoussé ; `movable: false` : un mur fixe.

Deux colliders interagissent si `a.layer & b.mask` et `b.layer & a.mask`.

```js
const box = parent.addComponent("Collider", { size: {x: 1, y: 2}, debugDraw: true });
parent.velocity = vec3(4, 0, 0);          // s'arrete contre les murs
const r = box.moveAndCollide({x: vx * dt, y: vy * dt, z: 0});   // ou a la main
if (r.blockedY) vy = 0;
box.onBeginOverlap(other => print("touche", other.id));
box.onHit((other, normal) => print("contre", other ? other.id : "un voxel", normal.y));
box.overlapping();            // acteurs
box.overlapsVoxels();
box.voxelFlags = ["ROCK"];    // voxels bloquants (defaut : voxels pleins)

// Dans une classe d'acteur ou un script attache :
function OnBeginOverlap(other) { }
function OnEndOverlap(other) { }
function OnHit(other, normal) { }
```

En C++, les mêmes événements sont des méthodes virtuelles de `lynx::Actor` :

```cpp
class Coin : public lynx::Actor
{
    void OnBeginOverlap(lynx::Actor* other) override { /* ramasse */ }
    void OnEndOverlap(lynx::Actor* other) override { }
    void OnHit(lynx::Actor* other, const lynx::vec3& normal) override { }
};
```

`size`, `offset`, `trigger`, `movable`, `generateOverlapEvents`, `layer`, `mask`,
`collideWithVoxels`, `voxelFlags`, `debugDraw`, `isOverlapping(acteur)`, `onEndOverlap(fn)`,
`resolvePenetration()`. Pas de gravité ni de rebond : le blocage arrête le mouvement, sans
traverser les murs fins.

### SoundSource

```js
const s = parent.addComponent("SoundSource", { sound: "footstep1.wav", volume: 0.8, pitchVariation: 0.1 });
s.play(); s.stop(); s.playing; s.playAt(vec3(0, 0, 0));
```

`sound`, `spatial` (3D qui suit l'acteur / 2D), `loop`, `playOnBegin`, `volume`, `pitch`,
`volumeVariation`, `pitchVariation`, `referenceDistance`, `maxDistance`, `rolloff`.

### Light

`type` (`"point"` / `"sky"`), `color`, `intensity`, `offset`, `enabled`.

### Fog / VolumetricFog

`Fog` : brouillard de distance de toute la scène (le dernier activé gagne). `mode`
(`"linear"`, `"exponential"`, `"exp2"`), `enabled`, `color`, `density` (modes exponentiels),
`start` / `end` (mode linéaire), `active` (lecture seule), `refresh()`.

`VolumetricFog` : brume volumétrique. Une boule autour de l'acteur (`radius`, `offset`), ou
toute la scène avec `global: true` (une seule à la fois). `enabled`, `color`, `density`,
`steps` (4 à 64), `refresh()`. Au plus 64 volumes par scène.

```js
this.addComponent("Fog", { mode: "exponential", color: {x: 0.5, y: 0.55, z: 0.65}, density: 0.02 });
this.addComponent("VolumetricFog", { radius: 18, density: 0.5 });
```

Retirer le composant (ou détruire l'acteur) enlève son brouillard.

### Light2D

Lumière de l'éclairage 2D (voir *Éclairage 2D*). `color`, `intensity`, `radius` (voxels),
`falloff` (1 linéaire, 2 doux, 4 serré), `offset`, `enabled`, `castShadows`, `shadowStrength`,
`sourceRadius` (0 ombres nettes, 1 à 3 ombres douces), `coneAngle` (360 : tout autour, 40 à 90 : lampe
torche), `coneSoftness`, `direction` (degrés, 0 = +X, + rotation Z de l'acteur si `useActorRotation`),
`flicker` (0.1 à 0.3 : torche).

```js
this.addComponent("Light2D", { color: {x: 1, y: 0.6, z: 0.3}, radius: 18, flicker: 0.2 });
```

## Acteurs du moteur

`Actor`, `PointLightActor`, `SpotLightActor`, `DirectionalLightActor`, `SkyLightActor`,
`SpriteActor`, `SoundActor`, `ColliderActor`, `Humanoid`, `Light2DActor`, `FogActor`
(brouillard de distance : `enabled`, `mode`, `color`, `density`, `start`, `end`) et
`VolumetricFogActor` (brume : `enabled`, `global`, `color`, `density`, `radius`, `steps`) et
`ParticleActor` (`system` : un `.vfx`, `auto_play`, `time_scale`, `visible`) sont enregistrés par le moteur (`gameplay/EngineActors.h`) :
`Level.spawn("PointLightActor")`, et une classe JS peut en hériter
(`class Torch extends PointLightActor { ... }`).

`ColliderActor` : une boîte de collision posée dans le niveau (propriétés `size`, `trigger`,
`movable`, `layer`, `mask`, `show_in_game`), dessinée dans l'éditeur (vert : trigger, rouge :
bloquant). Mur ou plateforme invisible quand elle bloque, zone quand c'est un trigger :

```js
class Checkpoint extends ColliderActor {
    constructor() { super(); this.trigger = true; }
    OnBeginOverlap(other) { if (other.hasTag("player")) print("checkpoint !"); }
}
```

`Humanoid` : un personnage qui marche, saute et tombe (collider bloquant, gravité, marches,
saut variable, double saut). Il ne lit aucun input : c'est sa classe (ou un script, une IA)
qui appelle `Move(dir)`, `Jump()`, `StopJumping()`, `Launch(x, y)`, `StopMovement()`.
Requêtes : `IsGrounded()`, `IsFalling()`, `IsFacingRight()`, `GetVelocity()`, `GetJumpCount()`,
`IsGroundWithinDistance(d)`. Événements : `OnLanded()`, `OnJumped()`. Réglages (propriétés) :
`collider_size`, `move_speed`, `jump_speed`, `gravity`, `max_fall_speed`, `air_control`,
`max_jump_count`, `max_step_height`, `face_movement_direction`, `show_collider`...

```js
class Hero extends Humanoid {
    ProcessInput(player) {
        this.Move(Input.axis("MoveRight"));
        if (Input.pressed("Jump")) this.Jump();
        if (Input.released("Jump")) this.StopJumping();
    }
    OnLanded() { print("boum"); }
}
```

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


## Anim Graph (`.animgraph`)

Graphe d'animation d'un `AnimationSprite`, fait dans l'éditeur (Content Browser > New file >
*Anim Graph*, double-clic pour l'ouvrir), comme l'AnimGraph d'Unreal : on part de la **sortie**.

- **Output Pose** : le nœud final, la pose jouée par le sprite. On y branche une **Animation**,
  un **Blend Space** ou une **State Machine** (clic droit sur le graphe, ou glisser une animation
  de la colonne de gauche).
- **State Machine** (double-clic pour l'ouvrir) : `Entry` (l'état de départ), `Any State`, les
  états et les transitions. Chaque **état** a sa pose (double-clic) : une Animation ou un Blend
  Space branché sur son Output Pose. Le chemin en haut du graphe (`AnimGraph > Locomotion > Idle`)
  ou Retour arrière : remonter.
- **Parameters** (`float`, `bool`, `int`, `trigger`) : valeurs données par le jeu
  (`setFloat`, `setBool`, `setInt`, `setTrigger`). Un trigger est consommé par la transition qui l'utilise.
- **Animations** : planche horizontale (`texture`, `frames`, `frame time`, `loop`) et
  **notifies** : frame → nom d'une fonction JS de l'acteur.
- **Blend spaces** : un paramètre float choisit l'animation (échantillon le plus proche en dessous).
- **States** : `next` (à la fin, sans transition), `on enter` / `on exit` (fonctions JS de
  l'acteur), `interruptible`, `restart on enter`.
- **Transitions** : glisser la broche d'un état sur un autre. Toutes leurs conditions doivent
  être vraies : `compare` (`speed > 0.1`), `isTrue`, `isFalse`, `trigger`, `finished`
  (fin de l'animation), `script` (fonction JS de l'acteur qui retourne `true` / `false`).
  `Any State` : transitions testées depuis n'importe quel état (blessure, mort...).

Les fichiers de l'ancienne version (états à la racine + `Entry`) s'ouvrent comme une State
Machine `Locomotion` branchée sur Output Pose.

Les événements appellent la méthode de la classe JS de l'acteur (ou la fonction d'un script
attaché) du même nom, et `AnimationSpriteComponent::on_graph_event` en C++ :

```js
class Hero extends Actor {
    BeginPlay() { this.anim = this.addComponent("AnimationSprite", { graph: "anim/Hero.animgraph" }); }
    Update(dt) { this.anim.setFloat("speed", Math.abs(this.velocity.x)); }
    OnFootstep() { this.getComponent("SoundSource")?.play(); }   // notify de l'animation Run
    CanAttack() { return this.stamina > 10; }                   // condition "script"
}
```

C++ : `sprite.graph = "anim/Hero.animgraph";` (ou `LoadGraph`), puis `SetFloat`, `SetTrigger`...


## IA : Behavior Trees (`.bt`)

Arbre de comportement façon Unreal, fait dans l'éditeur (New file > *Behavior Tree*), exécuté
par le composant **`AI`** de l'acteur :

```js
class Guard extends Actor {
    BeginPlay() {
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Guard.bt" });   // démarre tout seul
        this.ai.blackboard.set("home", this.position);
    }
}
```

| `AI` | |
|---|---|
| `behaviorTree` | asset `.bt` (le changer recharge l'arbre) |
| `startOnBegin` | démarre au lancement (`true`) |
| `running`, `activeNodes`, `loadedTree` | état (lecture) |
| `start()`, `stop()`, `restart()`, `load(asset)` | `stop` envoie `Abort` à la tâche en cours |
| `blackboard` | mémoire de l'IA (ci-dessous) |

**Exécution** : la racine a un enfant ; un **Selector** essaie ses enfants jusqu'à un succès,
une **Sequence** les enchaîne jusqu'à un échec. Les enfants s'exécutent **de haut en bas**
(leur hauteur dans l'éditeur ; le numéro dans le titre donne l'ordre). L'arbre recommence quand
il se termine.

- **Tâches** : `Wait`, `MoveTo` (vers une clé vecteur ou acteur, utilise le Collider s'il y en
  a un, peut piloter un paramètre `speed` de l'Anim Graph), `SetValue`, `ClearValue`,
  `SetAnimParam`, `Log` (`{clé}` = valeur du Blackboard), `CallFunction`, `Script`, `Finish`.
- **Décorateurs** (conditions / modificateurs d'un nœud) : `Blackboard`, `CallFunction`,
  `Script`, `Cooldown`, `Loop`, `TimeLimit`, `ForceSuccess`. Option **abort** (comme Unreal) :
  `self` arrête le nœud quand la condition devient fausse ; `lowerPriority` interrompt ce qui
  tourne plus bas (dans un Selector) quand elle devient vraie ; `both`.
- **Services** (tournent à intervalle tant que leur nœud est actif) : `CallFunction`, `Script`,
  `DistanceTo`, `FindNearest` (acteur le plus proche avec un tag).

### Blackboard

```js
const bb = this.ai.blackboard;          // ou this.blackboard dans un nœud JS
bb.set("target", Level.find("player")); // acteur, nombre, bool, texte, vecteur {x, y, z}
bb.get("target");                       // undefined si absent ; bb.get("hp", 100) : valeur par défaut
bb.has("target");  bb.clear("target");  bb.clear();  bb.keys();
```

Les clés déclarées dans l'éditeur ont un type (`int` arrondit, `bool`...) et une valeur par
défaut. Un acteur détruit n'est plus « set ».

### Nœuds écrits en JavaScript

N'importe quel `.js` de `assets/` ; le bouton **JS** des Details copie un squelette.
Valeur de retour : `true` / `undefined` / `BT.Success` = succès, `false` / `BT.Failure` = échec,
`BT.Running` = pas fini (la tâche est rappelée avec `Tick`).

```js
class Attack extends BTTask {            // nœud "Script", Class = Attack
    Execute(dt) {                        // au démarrage
        this.owner.getComponent("AnimationSprite").setTrigger("attack");
        this.t = 0;
        return BT.Running;
    }
    Tick(dt) { return (this.t += dt) > this.params.duration ? BT.Success : BT.Running; }
    Abort() { }                          // interrompue (abort d'un décorateur, stop())
}

class CanSeePlayer extends BTDecorator {
    Check() {
        const p = Level.find("player");
        return p && Math.abs(p.position.x - this.owner.position.x) < 8;
    }
}

class Perception extends BTService {
    Activated() { }  Tick(dt) { /* ... */ }  Deactivated() { }
}
```

Dans un nœud : `this.owner` (l'acteur), `this.blackboard`, `this.params` (les autres attributs
du nœud dans le fichier : `<Node type="Script" class="Attack" duration="0.4"/>` →
`this.params.duration === 0.4`), `this.name` (la classe). Une instance par nœud et par acteur.

`CallFunction` (tâche, décorateur, service) appelle simplement une méthode de l'acteur :
`Heartbeat(dt)` → même valeur de retour qu'une tâche.

En C++ : `actor->AddComponent<lynx::BehaviorTreeComponent>().behavior_tree = "ai/Guard.bt";`
puis `GetBlackboard().SetActor("target", player)`.

### Debug

Pendant le jeu, l'éditeur du `.bt` / `.animgraph` ouvert met en vert les nœuds actifs (ou l'état
joué) et affiche les valeurs du Blackboard / des paramètres en direct ; la liste *Debug* de la
barre d'outils choisit l'acteur.

