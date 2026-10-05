# {{PROJECT_TITLE}}

Jeu vu de dessus pour le moteur Lynx (template « Top-down (JavaScript) »).
Tout le gameplay est en JavaScript ; le C++ (`src/Module.cpp`) ne fait que la caméra.

```
assets/
  world.xml          le niveau (héros, gemmes)
  classes/Hero.js    le héros (déplacement 4 directions)
  classes/Gem.js     les gemmes à ramasser
  voxels.json        types de voxels (sol, pierre, eau...)
  sprites/           images
src/Module.cpp       caméra qui suit "hero"
input.json           touches (fenêtre Input Settings)
```

- **Jouer** : Play dans l'éditeur. ZQSD / WASD ou les flèches.
- Les fichiers `.js` sont rechargés par l'éditeur dès qu'ils changent : pas besoin de recompiler.
- Le sol est fait de voxels sans collision (« Floor ») : le héros marche dessus.
- **Lumières** : `sky_light` (ambiance), `sun` (soleil, tourné avec la rotation) et des `PointLightActor` / `SpotLightActor` dans `world.xml`. Ajouter une lumière : Place Actors > Engine. Leurs réglages (couleur, intensité, ombres...) sont dans Details.
