# Lynx Engine

Lynx est un moteur de jeu orienté 2D/pixel art et mondes voxel destructibles. Il combine un cœur C++, un système d'entités à composants et des scripts JavaScript exécutés avec QuickJS. L'objectif est de permettre de créer et de tester un jeu directement depuis l'éditeur, sans devoir écrire de C++ pour le gameplay courant.

## Fonctionnalités

- **Éditeur intégré** : viewport, placement d'Actors, panneau de propriétés (*Details*), explorateur de contenu et peinture de voxels.
- **Système Actor / Level / ECS** : organisation des objets du jeu avec des composants et gestion des niveaux.
- **Scripting JavaScript** : scripts attachés aux Actors, fonctions de cycle de vie (`BeginPlay`, `Update`, `EndPlay`), accès aux objets, aux transformations, aux propriétés et aux entrées.
- **Système d'input** : configuration d'actions et d'axes pour clavier, souris et manette.
- **Mondes voxel** : types de voxels configurables, couleurs, collisions, effets émissifs et règles de destruction, avec destruction possible en jeu.
- **Animation** : animations, blend spaces, transitions d'état, événements d'images, paramètres et déclencheurs.
- **Audio spatial** : sources audio attachées aux Actors, volume, pitch, boucle et spatialisation.
- **Intégration Git** : gestion des branches et remotes, staging, commits et historique depuis l'éditeur.
- **Rechargement du jeu** : remplacement de la DLL du jeu sans redémarrer l'éditeur.
- **Lynxie, assistant IA intégré** : aide à la gestion des fichiers, à l'écriture de scripts, à la modification de scènes et de terrains/voxels, ainsi qu'à la compilation du jeu. Le modèle utilisé est sélectionnable.
- **Extension en C++** : possibilité d'ajouter des classes et composants natifs lorsque le JavaScript ne suffit pas.

## Technologies

- **C++** — cœur du moteur et composants natifs
- **JavaScript / QuickJS** — scripts de gameplay
- **EnTT** — registre ECS
- **HRL (Horizon Rendering Library)** — rendu
- **Dear ImGui** — interface de l'éditeur (version modifiée)
- **GLFW**, **OpenAL**, **dr_libs**, **cURL** et **Tracy** — fenêtre/entrées, audio, décodage audio, réseau et profilage

## Documentation

Le site de documentation du projet présente notamment :

- l'architecture du moteur ;
- le guide de scripting JavaScript et la référence de l'API ;
- les capacités de Lynxie ;
- les bibliothèques tierces ;
- les téléchargements du launcher.

## Auteur

Développé par **Oscar Soirey**.
