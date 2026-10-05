# {{PROJECT_TITLE}}

Jeu de plateforme pour le moteur Lynx (template « Plateforme 2D »).

```
assets/
  world.xml          le niveau (joueur, pièces)
  voxels.json        types de voxels (herbe, terre, pierre...)
  classes/Coin.js    pièce, en JavaScript
  sprites/           images
src/
  Player.h / .cpp    le joueur : gravité, saut, collisions avec les voxels
  Module.cpp         classes du jeu + caméra qui suit "player"
commands/            scripts Python lancés depuis l'éditeur
input.json           touches (fenêtre Input Settings)
```

- **Jouer** : Play dans l'éditeur. Flèches ou A / D pour bouger, Espace / W / Haut pour sauter.
- **Lumières** : `sky_light` (ambiance), `sun` (soleil, tourné avec la rotation) et des `PointLightActor` / `SpotLightActor` dans `world.xml`. Ajouter une lumière : Place Actors > Engine. Leurs réglages (couleur, intensité, ombres...) sont dans Details.
- **Régler le joueur** : sélectionner `player`, fenêtre Details (vitesse, saut, gravité...).
- **Compiler** : bouton Compile de l'éditeur, ou `Build.bat`.
- **Ajouter une classe JS** : un fichier `assets/classes/MaClasse.js` (`class MaClasse extends Actor`).
