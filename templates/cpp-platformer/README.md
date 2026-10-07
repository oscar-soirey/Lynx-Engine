# {{PROJECT_TITLE}}

Le template « Plateforme 2D » écrit en **C++** : même niveau, mêmes voxels, mêmes assets.

```
src/
  Game.h         Hero (lynx::Humanoid), Coin, Slime
  Hero.cpp       entrées, dash, frappe au sol (lynx::voxels::DestroyRect), vie, HUD (lynx::CreateWidget)
  Actors.cpp     Coin (interface Collector), Slime (lynx::physics::Raycast)
  Module.cpp     classes enregistrées, interface "Collector" déclarée
assets/          niveau, voxels.json, ui/Hud.widget
```

- **Compiler** : bouton Compile de l'éditeur, ou `Build.bat`.
- **Jouer** : A / D, Espace (double saut), Shift dash, S frappe au sol, R recommencer.
- Les classes C++ et JavaScript se parlent par les mêmes interfaces : une classe JS peut
  implémenter `"Collector"` et ramasser les pièces C++.
