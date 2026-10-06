# Lynx Editor

Exécutable de l'éditeur, compilé avec le moteur (et non plus dans le jeu).

```
LynxEditor.exe                      -> navigateur de projets
LynxEditor.exe "C:/chemin/MonJeu"   -> ouvre ce projet directement
```

## Projet de jeu

```
MonJeu/
  assets/     <- assets du jeu (*.level + *.hrlv, save_file.txt = niveau de départ, input.json, cur/, ...)
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

## Compiler le jeu pendant que l'éditeur tourne

L'éditeur charge une **copie** de la DLL du jeu (dans `%TEMP%\LynxEditor\`),
jamais `build/<jeu>.dll` elle-même : le fichier n'est pas verrouillé et le jeu
peut être recompilé à tout moment.

- Bouton **Compile** de la barre d'outils : lance `Build.bat` du projet (ou
  `cmake --build build` s'il n'y en a pas) en arrière-plan. La sortie s'affiche
  dans la fenêtre *Build* (erreurs en rouge, bouton *Cancel*). Si la compilation
  réussit, le jeu est rechargé.
- Compilation en dehors de l'éditeur (`Build.bat`, CLion...) : l'éditeur voit
  que `build/<jeu>.dll` a changé et recharge le jeu.
- Pendant *Play*, le rechargement attend *Stop*.
- Options dans *Settings* : « Reload the game after Compile » et « Reload the
  game when its DLL is rebuilt ».
- Si la nouvelle DLL ne se charge pas, l'éditeur revient à la précédente.

`cmake` et MinGW doivent être dans le `PATH` de l'éditeur pour que *Compile*
fonctionne (ce qui est le cas si `Build.bat` marche dans un terminal).

## Compilation automatique à l'ouverture

Avant de charger la DLL du jeu, l'éditeur vérifie qu'elle est à jour. Il compile
le jeu (`Build.bat`, sinon CMake) dans une petite fenêtre qui affiche la sortie
du build :

- **DLL absente** (jeu jamais compilé) : compilation obligatoire.
- **DLL plus ancienne que `lynx.dll`** (moteur recompilé depuis) : compilation
  obligatoire. Cette DLL n'est **jamais** chargée telle quelle, elle ferait planter
  l'éditeur.
- **Sources plus récentes que la DLL** (`src/`, `include/`, `CMakeLists.txt`...) :
  compilation. Si elle échoue, « Open with the previous build » ouvre quand même
  le projet avec l'ancienne DLL.

Si le build échoue : « Retry » après correction, « Other project... » ou « Quit ».
Si l'éditeur plante malgré tout, une boîte de dialogue indique le module en
cause (DLL du jeu ou moteur) au lieu de fermer sans rien dire.

## Commandes (Python / IA)

Fenêtre *Windows > Commands (Python / AI)* : scripts Python du projet
(`<projet>/commands/*.py`), **Lynxie** (assistant IA local via Ollama : il reçoit l'état
de la scène à chaque message et génère un script Python à relire puis lancer), console
de commandes, référence de toutes les commandes, et configuration MCP pour brancher un
modèle d'IA. L'éditeur écoute
sur `127.0.0.1` (port 7420 ou suivant) ; la connexion est décrite dans
`<projet>/.lynx/editor.json`. Détails : `python/README.md` (à la racine du moteur).

Code : `editor/commands/` (registre, serveur, runner Python, fenêtre) et
`editor/EditorCommands.inl` (les commandes elles-mêmes).

## Lynxie (assistant IA local)

Onglet *Lynxie* de la fenêtre Commands : le modèle (Ollama) reçoit à chaque message
l'état de la scène (acteurs, cellules, cibles probables avec leurs propriétés, types
de voxels), écrit un script Python, *Save and run* le lance.

- **Auto-fix errors** (coché par défaut) : si le script échoue, ses changements sont
  annulés, la sortie d'erreur est renvoyée au modèle, et le script corrigé est relancé
  (2 à 5 essais). Le contexte est toujours reconstruit pour la demande d'origine.
- Commandes `ai.context` (le prompt exact pour une demande) et `ai.fix_message` :
  utilisées par l'évaluation (`python/lynxie_eval/`, voir son README).

## Content Browser

Clic droit sur un élément : *Open*, *Rename* (F2), *Duplicate* (Ctrl+D), *Copy* (Ctrl+C),
*Cut* (Ctrl+X), *Paste into* (dossiers), *Copy path*, *Show in Explorer*, *Delete* (Suppr).
Clic droit dans le vide : *New folder*, *New file* (texte, classe JavaScript, script
JavaScript attaché, niveau `.xml`, JSON), *Paste* (Ctrl+V), *Open in Explorer*.
Un clic sélectionne un fichier ; Entrée l'ouvre.

- Les fichiers supprimés sont **déplacés** dans `.lynx/trash/<date>/` (récupérables).
- Une nouvelle classe JS a un modèle prêt (`class NewActor extends Actor`) et s'ouvre dans
  l'éditeur JavaScript ; dupliquer `Enemy.js` renomme la classe de la copie (`Enemy_1`).
- Après une opération sur un `.js`, les scripts sont rechargés.

## Git

Fenêtre *Windows > Git* : utilise l'exécutable `git` du PATH (rien à configurer), sans
fenêtre de console, sur un thread (l'éditeur ne bloque jamais).

- Projet sans dépôt : *Initialize a repository* (`git init -b main` + un `.gitignore`
  Lynx : `build/`, `.lynx/`, `imgui.ini`, `editor_windows.txt`, `editor_camera.txt`...).
- **Changes** : fichiers modifiés / nouveaux / supprimés ; la case à cocher = stage / unstage ;
  clic = diff coloré ; clic droit = *Discard changes* (avec confirmation). Message +
  *Commit*, *Amend*, *Stage all + Commit*. Si le niveau n'est pas sauvegardé, un bouton
  *Save level* est proposé avant le commit.
- Barre du haut : branche (changer / *New branch...*), *Fetch*, *Pull* (`--ff-only`),
  *Push* (`-u origin <branche>` la première fois), en avance / en retard.
- **History** : les 100 derniers commits, clic = détails et diff du commit.
- **Settings** : URL du remote `origin`, nom / email de l'auteur, création du `.gitignore`.
- La sortie de chaque commande git s'affiche en bas de la fenêtre. Les merges avec
  conflits restent à faire dans un terminal (ou un client Git).

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

## Moteur (singleton)

```cpp
lynx::Engine::SetReleaseMode(true);          // optionnel, avant Create() : assets en archive
lynx::Engine* engine = lynx::Engine::Create();
// ... HRL_Init + HRL_InitContext ...
engine->CreateScene();                       // la scène HRL appartient au moteur
lynx::Engine::Get();                         // partout ensuite
lynx::Engine::GetScene();
lynx::Engine::Destroy();
```

`lynx::GetEngine()` / `lynx::GetScene()` existent encore (marqués `[[deprecated]]`) pour
que les projets de jeu compilent ; remplace-les puis supprime-les de `core/Engine.h`.

## Ce que la DLL du jeu fournit

Fonctions exportées **optionnelles**, décrites dans `core/GameModuleAPI.h` :

| Fonction | Rôle |
|---|---|
| `LynxGame_SetupScene(scene)` | avant le niveau (et après chaque *Reload Game*) : enregistre les événements de voxels |
| `LynxGame_OnGameStart()` / `LynxGame_OnGameEnd()` | début / fin de partie : runtime, *Play* / *Stop* de l'éditeur |
| `LynxGame_OnLevelLoaded(camera)` | niveau (re)créé : démarrage, *Reload Game*, *Stop* |
| `LynxGame_SetCollisionDebugEnabled(enabled)` | overlay de collisions (F4) |

La caméra n'est plus gérée par le jeu : c'est un `CameraComponent` de l'acteur possédé
par le `PlayerController`, affiché dans le viewport de ce joueur (`LynxGame_UpdateGameplayCamera`
n'est plus appelée).

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

- `EditorMain.cpp`, `ProjectBrowser.cpp`, `InputSettingsEditor.cpp`, `GameBuild.cpp`, `../host/GameProject.cpp`
- ImGui (branche docking) + backends GLFW / OpenGL3, ImGuiColorTextEdit (`TextEditor.cpp`)
- les mêmes includes et bibliothèques que l'ancienne cible du jeu avec éditeur
  (lynx, hrl, glfw, opengl32), `third-party/stb`

Aucune bibliothèque Windows supplémentaire (le dialogue de dossier charge
`ole32.dll` à l'exécution).

La DLL du jeu doit être compilée avec la même version du moteur que l'éditeur :
elle utilise le `lynx.dll` déjà chargé par l'éditeur.

Police de l'éditeur : `normal-font.ttf` à côté de `LynxEditor.exe` si présent,
sinon celle du projet (`assets/normal-font.ttf`), sinon celle d'ImGui.

## Ship Game : moteur Release et icône

- `build-release.bat` (à la racine du moteur, à côté de `build.bat`) compile le
  moteur en **Release** dans `build-release\` (même générateur CMake que
  `build\`). Ship Game prend alors `LynxRuntime.exe` et les DLL du moteur dans
  ce dossier (choix *Release* / *Editor build* dans la fenêtre). Si la Release
  est plus ancienne que le moteur de l'éditeur, la fenêtre le signale : relancer
  `build-release.bat`.
- **Icon** : un `.ico` (utilisé tel quel) ou une image (png, jpg, bmp, tga),
  convertie en 16 à 256 px. Elle est écrite dans les ressources de l'exécutable
  sous le nom `GLFW_ICON` : l'Explorateur, la barre des tâches et la fenêtre du
  jeu l'utilisent. Par défaut : `assets/icon.png` du projet.
- Les réglages de Ship Game sont gardés par projet dans `.lynx/ship.cfg`.

## Widget Editor (UI, type UMG d'Unreal)

Content Browser > clic droit > *New file* > **Widget (.widget, UI)**, puis
double-clic sur le fichier. La fenêtre s'ouvre à côté du Viewport :

- **Palette** : glisser un widget sur le Designer ou dans la Hierarchy
  (double-clic : dans le panel sélectionné). *User Widgets* : les autres
  `.widget` du projet, utilisables comme un widget.
- **Hierarchy** : glisser pour déplacer / changer de parent ; clic droit :
  Rename, Duplicate, Copy / Paste, Wrap With (un panel), Move Up / Down, Delete.
- **Designer** : clic = sélection, glisser = déplacer (enfants d'un
  CanvasPanel), poignées = redimensionner, molette = zoom, clic droit / milieu
  = déplacer la vue, F = tout voir. Résolution d'aperçu dans la barre d'outils
  (la mise en page suit le DPI scale comme en jeu). Snap à la grille.
- **Details** : le slot du widget (ancres avec presets 4 x 4 : Shift = aussi
  l'alignement, Ctrl = aussi la position), puis ses champs. Sans sélection :
  la classe C++ du widget et *Copy C++ class* (squelette avec un pointeur par
  widget nommé).
- Raccourcis : Ctrl+S, Ctrl+Z / Ctrl+Y, Suppr, Ctrl+D, F2, Ctrl+C / Ctrl+V,
  flèches (Shift : pas de la grille).

La mise en page est calculée par le même code que le jeu (`lynx::UserWidget`) ;
l'aspect est dessiné par l'éditeur, proche du rendu HRL sans être identique.
