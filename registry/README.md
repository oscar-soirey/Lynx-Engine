# Registre des plugins

`plugins.json` est le catalogue lu par l'onglet **Plugins** du launcher
(`https://raw.githubusercontent.com/oscar-soirey/Lynx-Engine/main/registry/plugins.json`).
Une entrée par plugin ; chaque plugin vit dans son propre dépôt GitHub :

```json
[
  {
    "name": "Weather",
    "repo": "oscar-soirey/Lynx-Weather",
    "description": "Pluie, neige, cycle jour / nuit.",
    "category": "Gameplay",
    "author": "Oscar"
  }
]
```

`repo` accepte `owner/nom` ou l'URL `https://github.com/owner/nom`. `name` doit être le même que
le `name` du `plugin.json` du plugin (c'est ainsi que le launcher reconnaît un plugin déjà installé).

## Releases d'un plugin

Tag `v<version du plugin>-<version de Lynx>` : `v1.2.4-26.0.6` = plugin 1.2.4 fait pour Lynx 26.0.6.

- Le launcher propose, pour la version du moteur choisie, la plus haute version du plugin dont la
  version de Lynx est inférieure ou égale (un plugin pour 26.0.1 marche en 26.0.6).
- Si elle est plus ancienne que le moteur, l'éditeur prévient au moment de l'activer
  (Options > Plugins).
- Asset : le premier `.zip` de la release, avec `plugin.json` (et `bin/`) à la racine ou dans un
  sous-dossier. Les pré-releases ne sont proposées que si « Versions bêta » est coché.
- Les tags d'une autre forme sont ignorés.
