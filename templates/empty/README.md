# {{PROJECT_TITLE}}

Projet pour le moteur Lynx (template « Vide »).

```
assets/            assets du jeu (world.level + world.hrlv, voxels.json, sprites, classes JS...)
src/               code du jeu -> {{PROJECT_NAME}}.dll
  Module.cpp       classes enregistrées + hooks du moteur
  ExampleActor.*   un acteur d'exemple (propriétés, fonction appelable en JS / Python)
commands/          scripts Python lancés depuis l'éditeur
input.json         touches (fenêtre Input Settings)
```

- **Lumières** : `sky_light` (ambiance), `sun` (soleil, tourné avec la rotation) et des `PointLightActor` / `SpotLightActor` dans `world.level`. Ajouter une lumière : Place Actors > Engine. Leurs réglages (couleur, intensité, ombres...) sont dans Details.
- **Compiler** : bouton Compile de l'éditeur, ou `Build.bat` (MinGW, comme le moteur).
- **Ajouter une classe C++** : la déclarer dans `src/`, puis `LYNX_MODULE_REGISTER(MaClasse);` dans `Module.cpp`.
- **Ajouter une classe JavaScript** : `assets/classes/MaClasse.js` avec `class MaClasse extends Actor { ... }`.
