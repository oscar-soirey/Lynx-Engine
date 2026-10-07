# {{PROJECT_TITLE}}

Shooter vu de dessus pour le moteur Lynx (template « Top-down shooter »), **tout en JavaScript**.

```
assets/
  world.level / world.hrlv   l'arène
  classes/Gunner.js          le joueur : twin-stick, tir, dash
  classes/Bullet.js          balles (Physics.raycast), impacts qui détruisent la couverture
  classes/Robot.js           ennemi : méthodes appelées par son Behavior Tree, soins
  classes/WaveDirector.js    vagues, points d'apparition, score
  ai/Robot.bt                l'IA des robots (éditeur de Behavior Tree)
  ui/*.widget                HUD
input.json                   touches
```

- **Jouer** : WASD bouger, flèches viser, Espace / J / clic tirer, Shift dash (invulnérable), R rejouer.
- Les caisses et les murets (voxels avec le flag `COVER`) se détruisent sous les balles.
- Les robots (Behavior Tree) cherchent le joueur, tirent s'ils le voient (`Physics.lineOfSight`),
  sinon se rapprochent.
