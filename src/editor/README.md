# Lynx Editor

Exécutable de l'éditeur, compilé avec le moteur (et non plus dans le jeu).

```
LynxEditor.exe                      -> navigateur de projets
LynxEditor.exe "C:/chemin/MonJeu"   -> ouvre ce projet directement
```

## Projet de jeu

```
MonJeu/
  assets/     <- assets du jeu (world.xml, save_file.txt, input.json, cur/, ...)
  build/      <- la DLL du jeu (ex : Blocky.dll), ou build/<config>/
```

La DLL du jeu est trouvée automatiquement : c'est celle de `build/` qui exporte
`FactoryRegisterClasses` (macro `LYNX_LINK_MODULE`). S'il y en a plusieurs, le
navigateur propose une liste (par défaut la plus récemment compilée).

À l'ouverture, le dossier de travail devient la racine du projet : `assets/`,
`settings.cfg`, `editor_camera.txt`, `editor_windows.txt`, `imgui.ini` sont ceux
du projet. Seule la liste des projets récents (`editor_recent_projects.txt`) est
à côté de l'exécutable de l'éditeur.

Barre d'outils > bouton du projet > *Open another project...* : ferme l'éditeur
(avec la confirmation de sauvegarde habituelle) et relance le navigateur.

## Input Settings

Fenêtre *Input Settings* (bouton des fenêtres de la barre d'outils) : édite
`input.json` à la racine du projet, comme les Input Settings d'Unreal.

- **Action Mappings** (`lynx::InputAction`) et **Axis Mappings**
  (`lynx::InputAxis1D`, avec un *scale* par touche).
- Une touche se choisit dans la liste (flèche, avec recherche), ou en cliquant
  sur son bouton puis en appuyant sur la touche voulue : clavier, bouton de
  souris, molette (axes), bouton / stick / gâchette de manette. Échap annule.
- Chaque modification est sauvegardée et rechargée tout de suite : le jeu
  utilise les nouvelles touches sans redémarrer.

Format : les touches clavier sont écrites avec leur nom (`KEY_SPACE`, `KEY_W`,
`KEY_LEFT_SHIFT`...) ; les anciens codes numériques (`"32"`) restent acceptés.
Les noms correspondent à la position sur un clavier QWERTY (codes GLFW), mais
l'éditeur affiche la lettre de ton clavier (`KEY_Q` s'affiche « A » en AZERTY).

## Types de voxels

Décrits dans `assets/voxels.json` (format : `src/core/Voxels.h`), chargés par
l'éditeur et le runtime avant le niveau. Le jeu les lit avec `lynx::voxels`
(`GetFlags`, `GetFlagMask("ROCK")`, `IsIndestructible`, `GetFlagsBelow`...).
*Reload Game* relit aussi `voxels.json`.

## Ce que la DLL du jeu fournit

Fonctions exportées **optionnelles**, décrites dans `core/GameModuleAPI.h` :

| Fonction | Rôle |
|---|---|
| `LynxGame_SetupScene(scene)` | avant le niveau (et après chaque *Reload Game*) : enregistre les événements de voxels |
| `LynxGame_OnGameStart()` / `LynxGame_OnGameEnd()` | début / fin de partie : runtime, *Play* / *Stop* de l'éditeur |
| `LynxGame_OnLevelLoaded(camera)` | niveau (re)créé : démarrage, *Reload Game*, *Stop* |
| `LynxGame_UpdateGameplayCamera(camera, dt)` | caméra de jeu, chaque frame |
| `LynxGame_SetCollisionDebugEnabled(enabled)` | overlay de collisions (F4) |

Une fonction absente est simplement ignorée. Exemple complet : `src/Module.cpp`
du projet Blocky.

## Projets de jeu séparés

Un projet de jeu (ex : Blocky) est un dossier indépendant, n'importe où sur
l'ordinateur, avec son propre `CMakeLists.txt` :

```cmake
find_package(Lynx CONFIG)          # trouvé via le registre CMake
add_library(MonJeu SHARED ...)
target_link_libraries(MonJeu PRIVATE Lynx::Lynx Lynx::HRL)
```

Configurer le moteur génère `LynxConfig.cmake` dans son dossier de build et
l'enregistre dans le registre de paquets CMake de l'utilisateur
(`HKEY_CURRENT_USER\Software\Kitware\CMake\Packages\Lynx`). Pour forcer un
build précis du moteur : `-DLynx_DIR=<dossier de build du moteur>`.
Le jeu doit être compilé avec le même compilateur que le moteur (vérifié).

## Runtime

`LynxRuntime.exe` (`src/runtime`) joue un projet sans l'éditeur :

```
LynxRuntime.exe                     -> projet = dossier de travail
LynxRuntime.exe "C:/chemin/MonJeu"  -> projet = ce dossier
```

La DLL du jeu est cherchée à côté de l'exe (jeu distribué), puis dans
`build/` du projet.

`src/host/` contient le code commun aux deux exécutables (gamepad, projets) :
il n'est ni dans `lynx.dll` ni visible par le jeu.

## Compilation

`src/editor/` ne doit **pas** être compilé dans `lynx.dll` : c'est un exécutable
à part. Il a besoin de :

- `EditorMain.cpp`, `ProjectBrowser.cpp`, `InputSettingsEditor.cpp`, `../host/GameProject.cpp`
- ImGui (branche docking) + backends GLFW / OpenGL3, ImGuiColorTextEdit (`TextEditor.cpp`)
- les mêmes includes et bibliothèques que l'ancienne cible du jeu avec éditeur
  (lynx, hrl, glfw, opengl32), `third-party/stb`

Aucune bibliothèque Windows supplémentaire (le dialogue de dossier charge
`ole32.dll` à l'exécution).

La DLL du jeu doit être compilée avec la même version du moteur que l'éditeur :
elle utilise le `lynx.dll` déjà chargé par l'éditeur.

Police de l'éditeur : `normal-font.ttf` à côté de `LynxEditor.exe` si présent,
sinon celle du projet (`assets/normal-font.ttf`), sinon celle d'ImGui.
