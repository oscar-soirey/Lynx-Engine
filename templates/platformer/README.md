# {{PROJECT_TITLE}}

Jeu de plateforme pour le moteur Lynx (template « Plateforme 2D »), **tout en JavaScript** :
rien à compiler, les `.js` sont rechargés dès qu'ils changent.

```
assets/
  world.level / world.hrlv   le niveau (acteurs) et son monde de voxels
  voxels.json                types de voxels : textures, glace, boue, caoutchouc, pics, eau, lave...
  classes/Hero.js            le héros (Humanoid du moteur) : double saut, dash, frappe au sol, vie
  classes/Enemies.js         Slime : patrouille (raycasts), écrasé par un saut
  classes/Pickups.js         pièces et cœurs (interface Collector)
  classes/LevelActors.js     checkpoints et drapeau d'arrivée (ColliderActor en trigger)
  classes/Interfaces.js      les interfaces du jeu (Collector, Respawnable, Finisher)
  classes/Effects.js         petits effets (étincelles, débris)
  ui/Hud.widget              le HUD (Widget Editor)
input.json                   touches (fenêtre Input Settings)
```

- **Jouer** : Play. A / D bouger, Espace sauter (deux fois : double saut), Shift dash,
  S en l'air : frappe au sol (casse les briques, écrase les slimes), R : dernier checkpoint.
- **Voxels** : glace (on glisse), boue (on colle), caoutchouc (on rebondit), pics (dégâts),
  eau (on nage), lave (dégâts + lumière), sable qui tombe quand les briques en dessous cassent,
  cristaux qui éclairent. Tout se règle dans *Paint > Voxel types*.
- **Communication** : les classes ne se modifient jamais entre elles, elles s'envoient des
  messages d'interface (`other.send("Collector", "OnCollected", "coin", 1)`).
