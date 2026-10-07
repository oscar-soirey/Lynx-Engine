# Voxel Sandbox

Sandbox de voxels façon Noita pour le moteur Lynx (template « Sandbox voxels »), **tout en JavaScript**.

```
assets/
  world.level / world.hrlv   le niveau et son monde de voxels (grottes générées)
  voxels.json                terre, pierre, sable, gravier, eau, huile, lave, acide, or, cristal...
  classes/Miner.js           le mineur : outils, inventaire, vie
  classes/Bomb.js            bombe : rebonds (Physics.raycast), explosion (Voxels.destroyCircle)
  classes/Effects.js         explosion, étincelles, débris, coffre de départ
  ui/Hud.widget              le HUD
input.json                   touches
```

- **Jouer** : A / D bouger, Espace sauter, flèches viser, J / Ctrl / clic utiliser l'outil,
  1-4 (ou Q / E) changer d'outil, F matériau à construire, R retour au départ.
- **Outils** : pioche (ce qui est détruit va dans l'inventaire), construction (consomme
  l'inventaire), bombes, laser.
- **Simulation** : le sable, l'eau, l'huile, l'acide et la lave bougent autour de la caméra
  (`VoxelPhysics`). La lave et l'acide font des dégâts, la lave et les cristaux éclairent.
- **Objectif** : récolter de l'or (`gold_goal` dans les Details du mineur).
