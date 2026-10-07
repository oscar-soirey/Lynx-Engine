# {{PROJECT_TITLE}}

Petite aventure vue de dessus pour le moteur Lynx (template « Aventure / RPG »), **tout en
JavaScript**, avec les plugins **Dialogue** et **CineCamera**.

```
assets/
  world.level / world.hrlv   village, rivière, forêt et ruines
  classes/Adventurer.js      le joueur : déplacement, interaction (E), quête affichée
  classes/World.js           villageois (DialogueNPC), coffre, panneaux, porte, relique, lucioles
  dialogues/*.dialogue       dialogues (éditeur de graphe : double-clic)
  story/quests.json          les quêtes et leurs objectifs
  cine/intro.sequence        la cinématique d'intro (séquenceur : double-clic)
  ui/Hud.widget              quête en cours, invite d'interaction, messages
input.json                   touches (DialogueNext : passer une réplique)
```

- **Jouer** : WASD bouger, Shift courir, E parler / ouvrir / lire, Espace réplique suivante.
- **Déroulé** : cinématique, l'ancien donne la quête, la clé est dans un coffre de la forêt, la
  porte des ruines s'écroule (événement de dialogue `OpenGate` + caméra), la relique termine l'aventure.
