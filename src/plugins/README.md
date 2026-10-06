# Plugins (C++)

Un plugin ajoute des classes, des composants, des fonctions JS et des outils
d'éditeur, sans toucher au moteur. Il est fait de deux DLL :

| Module | Fichier | Chargé par | Contenu |
|---|---|---|---|
| runtime | `bin/<Nom>.dll` | l'éditeur **et** le jeu | acteurs, composants, widgets, `RegisterScriptFunction`, hooks de jeu |
| éditeur (optionnel) | `bin/<Nom>.Editor.dll` | l'éditeur seulement (jamais livré) | fenêtres, menus, éditeurs de fichiers (`.dialogue`...), nouveaux types de fichiers |

## Où sont les plugins

- `<éditeur>/plugins/<Nom>/` : plugins du moteur (livrés avec Lynx : **Dialogue**, **CineCamera**). Désactivés par défaut.
- `<projet>/plugins/<Nom>/` : plugins du projet. Activés par défaut.

Activation par projet : fenêtre **Plugins** (barre d'outils), sauvegardée dans `<projet>/plugins.json`
(`{ "Dialogue": true }`). Un changement demande de redémarrer l'éditeur.
**Ship Game** copie le module runtime des plugins activés dans `<jeu>/plugins/` (jamais le module éditeur).

## plugin.json

```json
{
  "name": "Dialogue",
  "version": "1.0",
  "description": "...",
  "author": "...",
  "category": "Gameplay",
  "runtime": "bin/Dialogue.dll",
  "editor": "bin/Dialogue.Editor.dll"
}
```

## Créer un plugin

Fenêtre **Plugins** > *New project plugin* : crée `plugins/<Nom>/` (plugin.json, CMakeLists.txt,
`Source/<Nom>.cpp`, `Source/<Nom>Editor.cpp`) et ajoute `add_subdirectory(plugins/<Nom>)` au
CMakeLists.txt du projet. Compiler le projet, redémarrer l'éditeur.

```cmake
include("${Lynx_DIR}/LynxPlugin.cmake")     # après find_package(Lynx)
lynx_add_plugin(MonPlugin
    RUNTIME Source/MonPlugin.cpp
    EDITOR  Source/MonPluginEditor.cpp       # optionnel
    IMNODES                                  # optionnel : graphes de nœuds dans l'éditeur
)
```

### Module runtime

```cpp
#include <plugins/LynxPlugin.h>

LYNX_PLUGIN(MonPlugin)                        // obligatoire

class Coffre : public lynx::Actor { /* ... */ };
LYNX_LINK_MODULE( LYNX_MODULE_REGISTER(Coffre); )

LYNX_PLUGIN_STARTUP()
{
    lynx::DefineInterface("Ouvrable", { "Ouvrir" });
    lynx::RegisterScriptFunction("MonPlugin", "bonjour",
        [](const lynx::InterfaceArgs& args) -> lynx::InterfaceArg { return std::string("salut"); });
    lynx::RegisterScriptPrelude("<MonPlugin>", "MonPlugin.version = 1;");
}
LYNX_PLUGIN_SHUTDOWN() {}
LYNX_PLUGIN_TICK(dt, playing) {}              // chaque frame (aussi dans l'éditeur)

// Hooks de jeu (GameModuleAPI.h) : LynxGame_OnGameStart, LynxGame_OnGameEnd,
// LynxGame_OnLevelLoaded, LynxGame_SetupScene
LYNX_GAME_EXPORT void LynxGame_OnGameStart() {}
```

Fichiers du plugin : `lynx::plugins::PluginAssetPath("MonPlugin", "data/x.json")`.

### Module éditeur

```cpp
#include <editor/EditorPluginAPI.h>

static void Fenetre(bool* open, void*) { if (ImGui::Begin("Mon outil", open)) { /* ... */ } ImGui::End(); }
static bool Ouvrir(const char* path, void*) { /* ... */ return true; }

LYNX_EDITOR_PLUGIN_STARTUP(api)
{
    LYNX_EDITOR_PLUGIN_INIT(api);                                   // contexte ImGui de l'éditeur
    api->add_window("MonPlugin", "Mon outil", Fenetre, nullptr, false);   // Windows > Plugins
    api->add_menu_item("MonPlugin", "Faire un truc", [](void*) {}, nullptr);  // menu Plugins
    api->add_file_editor("MonPlugin", ".truc", Ouvrir, nullptr);            // double-clic
    api->add_new_file("MonPlugin", "Truc (.truc)", "NouveauTruc", ".truc", "{ \"nom\": \"{{NAME}}\" }");
    api->add_before_level_snapshot("MonPlugin", [](void*) { /* annuler un aperçu */ }, nullptr);
}
LYNX_EDITOR_PLUGIN_SHUTDOWN() {}
```

État de l'éditeur : `api->get_selected_actor()`, `select_actor`, `mark_level_dirty`, `is_playing`,
`project_root`, `assets_root`, `message`, `open_asset`. Le moteur entier (`<Lynx.h>`) est disponible.

## Projets sans C++

Un projet sans `Build.bat` / `CMakeLists.txt` ni DLL dans `build/` utilise la DLL de jeu générique du
moteur (`<éditeur>/scriptgame/LynxScriptGame.dll`) : rien n'est compilé. Le jeu est fait de classes et
scripts JavaScript, des acteurs du moteur (`Humanoid`, lumières, colliders...) et des plugins.
Template **JavaScript only** dans *New project*.

## Plugins livrés

### Dialogue
- Fichiers `.dialogue` : éditeur de graphe (nœuds `line`, `choice`, `branch`, `set`, `event`, `quest`, `end`),
  conditions en JavaScript (`Story.get('coins') > 2`), `{variable}` dans les textes.
- Acteurs : `DialogueNPC` (interface `Interactable` : `lynx::Interact(npc, joueur)`), `DialogueTrigger` (zone).
- Boîte de dialogue par défaut (bouton *Continue*, choix ; action `DialogueNext` d'input.json), désactivable
  (`Dialogue.setDefaultBox(false)`) pour une UI à soi via l'interface `DialogueEvents`
  (`OnDialogueStarted`, `OnDialogueLine(speaker, text)`, `OnDialogueEvent(name)`, `OnDialogueEnded`).
- Histoire : variables et quêtes (`assets/story/quests.json`, fenêtre **Story**), sauvegardes
  `saves/<slot>.story.json`, interface `StoryEvents` (`OnStoryVariableChanged`, `OnQuestChanged`).
- JS : `Dialogue.start(path, speaker?, listener?)`, `next()`, `choose(i)`, `stop()`, `isActive()`, `current()` ;
  `Story.get/set/add/has/all/reset/save/load` ; `Quest.start/complete/fail/objective/state/list/get`.

### CineCamera
- `CineCameraActor` : focale (mm) ou FOV, `look_at` (id d'acteur), tremblement, `Activate()`, `CutTo(blend)`.
- `CameraRailActor` : déplace une caméra sur une spline (`points` relatifs, `duration`, `loop`, `progress`
  pour l'aperçu dans l'éditeur).
- Blends de caméra (vue du joueur 0), séquenceur `.sequence` : coupes caméra (avec blend), animation
  d'acteurs (clés, interpolation smooth / linear / step), événements, fondus (post process).
  Aperçu en déplaçant la tête de lecture dans le **Sequencer**.
- `SequencePlayer` : joue une séquence au lancement (ou `Play()`).
- Interface `SequenceEvents` : `OnSequenceEvent(name, sequence)`, `OnSequenceFinished(sequence)`.
- JS : `Cine.cutTo(actor | id, blend)`, `Cine.release()` ; `Sequence.play(path, loop)`, `stop`, `pause`,
  `resume`, `seek(t)`, `isPlaying`, `time`.
