# Lynx Launcher

Petit programme qui prépare une machine Windows pour Lynx. Son interface est en **Raven**
(Vulkan), comme l'éditeur : mêmes thèmes (celui choisi dans les options de l'éditeur est repris),
mêmes widgets partagés (`src/editor/interface/widgets.xml`) et même couche C++
(`src/editor/raven/RavenUi.*`). La fenêtre est décrite dans `interface/launcher.xml`.

## Onglets

- **Dépendances** : détecte et installe (via `winget`) ce dont Lynx a besoin :
  | Outil | Pourquoi | Paquet winget |
  |---|---|---|
  | Python 3.14 | scripts `commands/*.py`, `lynx_editor`, `lynx_mcp.py` | `Python.Python.3.14` |
  | CMake | projets du moteur et des jeux | `Kitware.CMake` |
  | MinGW-w64 (GCC) | compilateur du moteur et des jeux | `BrechtSanders.WinLibs.POSIX.UCRT` |
  | Git | fenêtre Git de l'éditeur, versions du moteur | `Git.Git` |
  | Ollama | IA locale de l'éditeur (Lynxie) | `Ollama.Ollama` |

  « Tout installer » installe ce qui manque, l'une après l'autre. Le PATH est
  relu dans le registre après chaque installation : pas besoin de relancer.
- **Modèles IA** : Ollama est installé sans modèle. On coche ceux qu'on veut
  (`qwen2.5-coder:7b` est conseillé, comme dans `python/README.md`), ou on
  saisit n'importe quel tag Ollama. Le launcher démarre `ollama serve` au besoin,
  affiche la progression de `ollama pull` et permet de supprimer un modèle.
- **Moteur** : les versions viennent des releases GitHub de
  [oscar-soirey/Lynx-Engine](https://github.com/oscar-soirey/Lynx-Engine/releases)
  (API GitHub lue avec `curl`). Chaque version est une carte : numéro,
  Beta / Stable, date, taille, notes de version, et les boutons
  *Installer* / *Lancer* / *Dossier* / *Retirer*. L'archive Windows de la
  release (`lynx-w-x64-binaries.zip`) est téléchargée puis extraite avec `tar`
  dans `%LOCALAPPDATA%\Lynx\versions\<tag>\` : plusieurs versions peuvent
  cohabiter. Le SDK de la version (`<tag>\sdk\cmake`, voir `cmake/LynxSdkConfig.cmake.in`)
  est enregistré dans le registre de paquets CMake de l'utilisateur
  (`HKCU\Software\Kitware\CMake\Packages\Lynx`, valeur `LynxLauncher-<tag>`) : `find_package(Lynx)`
  des projets de jeu le trouve. Les versions installées sont réenregistrées au démarrage, la valeur
  est retirée avec la version. *Lancer* ouvre `LynxEditor.exe` de la version. L'installation
  demande que toutes les dépendances soient présentes.
- **Versions locales** (*Ajouter une version locale...*, onglet Moteur) : un moteur compilé ou modifié
  soi-même, ou une version déjà installée ailleurs (autre disque, Microsoft Store...). On choisit le
  dossier de `LynxEditor.exe`, ou la racine d'un clone du moteur (`build-release/`, `build/`,
  `cmake-build-*/` y sont cherchés). La version est lue dans son SDK
  (`sdk/cmake/LynxConfigVersion.cmake`). La liste est gardée dans
  `%LOCALAPPDATA%\Lynx\local-engines.txt` (`tag<TAB>nom<TAB>dossier`, tag `local-<nom>`).
  Une version locale se lance, reçoit des plugins (son dossier `plugins/`) et est enregistrée pour
  `find_package(Lynx)` comme les autres ; *Oublier* la retire de la liste sans toucher à ses fichiers.

  `curl.exe` et `tar.exe` sont fournis avec Windows 10 (1803+) et 11.
- **Plugins** : plugins du moteur installés dans une version choisie
  (`%LOCALAPPDATA%\Lynx\versions\<tag>\plugins\<Nom>\`, le dossier `<éditeur>/plugins` de l'éditeur).
  - *En ligne* : catalogue `registry/plugins.json` du dépôt (voir `registry/README.md`), un dépôt GitHub
    par plugin, releases taguées `v<plugin>-<Lynx>` (`v1.2.4-26.0.6`). Pour la version choisie, la carte
    propose la plus haute version du plugin faite pour cette version de Lynx ou une plus ancienne :
    *Installer*, *Mettre à jour* (version plus récente compatible), *Dossier*, *Retirer*, *Dépôt*.
  - *Depuis l'ordinateur* : *Importer un dossier...* (dossier contenant `plugin.json`), ou glisser un
    dossier / un `.zip` sur la fenêtre.
  - Recherche (nom, auteur, description), filtre par catégorie, « Versions bêta » (pré-releases).
  - Les plugins livrés avec le moteur (Dialogue, CineCamera...) sont marqués *Intégré* : ni remplacés
    ni retirés.
  - Le launcher écrit `.lynx-install.json` dans le dossier du plugin (source, dépôt, tag, version de Lynx
    visée). L'éditeur le lit : un plugin fait pour une version plus ancienne demande une confirmation à
    l'activation (Options > Plugins). Sans ce fichier, `"engine_min"` du `plugin.json` est utilisé.
  - L'API GitHub sans compte est limitée à 60 requêtes / heure (une par plugin du catalogue à chaque
    *Actualiser*).

Un journal en bas de la fenêtre affiche la sortie des commandes.

## Compiler

Le launcher est une cible du CMakeLists racine du moteur, comme `LynxEditor`
et `LynxRuntime` : `cmake --build build --target LynxLauncher` (ou la cible
`LynxLauncher` dans CLion). Il utilise Raven (`RAVEN_DIR`, à côté du moteur comme pour l'éditeur).

Il ne dépend pas de `lynx.dll` et il est lié en statique (`-static`) : il doit
démarrer sur un ordinateur où MinGW n'est pas encore installé. Vulkan vient du pilote
graphique (`vulkan-1.dll`) ; sans Vulkan, un message le dit au démarrage.
`interface/launcher.xml`, les fichiers d'interface de l'éditeur, les icônes et la police sont
intégrés à l'exécutable (`cmake/LynxEmbedFiles.cmake`, en-tête `generated-launcher/EditorUiFiles.h`) :
rien à livrer à côté.

## Code

```
main.cpp          démarrage de Raven, fenêtre, boucle (Tick ~10 fois par seconde), glisser-déposer
interface/launcher.xml  la fenêtre : barre latérale, les 4 pages, le journal
Launcher.*        tâches en arrière-plan (un thread à la fois) et interface : les listes sont
                  reconstruites quand l'état change, barres de progression et journal mis à jour à chaque Tick
Dependencies.*    liste des dépendances, détection, installation winget
Models.*          catalogue de modèles, ollama list / pull / rm / serve
Engine.*          releases GitHub, téléchargement, extraction, lancement, versions locales
PluginStore.*     catalogue des plugins, tags, installation / import / retrait
Process.*         lancement de commandes avec lecture de la sortie, PATH
```

Pour ajouter une dépendance : un bloc dans `Dependencies::CreateList()`.
Pour ajouter un modèle au catalogue : une ligne dans `Models::Catalog()`.

## Mise à jour du launcher (LynxUpdater)

`LynxUpdater.exe` (dossier `updater/`) se lance **avant** le launcher. Il est autonome : l'installateur
ne livre que lui, il télécharge le launcher au premier lancement (fenêtre de progression affichée tout de suite).

Dossier du launcher : à côté de l'updater si ce dossier est modifiable, sinon
`%LOCALAPPDATA%\Lynx\launcher\` (installation dans *Program Files* : l'updater tourne sans droits
administrateur). Un launcher déjà présent dans `%LOCALAPPDATA%` est prioritaire.

1. il lit la dernière release GitHub dont le tag commence par `launcher` (les tags `v26.x`
   du moteur sont ignorés, brouillons et pré-releases aussi) ;
2. il compare avec le tag installé, écrit dans `launcher.version` à côté de l'exécutable ;
3. plus récent : il télécharge l'archive (`.zip` contenant `LynxLauncher.exe`, ou l'`.exe` seul),
   remplace les fichiers (l'updater n'est jamais écrasé) puis met `launcher.version` à jour,
   avec une petite fenêtre de progression ;
4. dans tous les cas il lance `LynxLauncher.exe`. Sans réseau ou si la mise à jour échoue,
   l'ancien launcher démarre quand même.

Si `launcher.version` est absent, la version est considérée comme inconnue et la dernière release est installée.
À chaque release du launcher : compiler avec `-DLYNX_LAUNCHER_TAG=launcher-v-X.Y.Z` (même valeur que le tag GitHub).
Les raccourcis doivent pointer vers `LynxUpdater.exe`.
