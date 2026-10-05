# Lynx Launcher

Petit programme (ImGui + GLFW) qui prépare une machine Windows pour Lynx.

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
- **Moteur** : se débloque quand toutes les dépendances sont installées.
  L'installation d'une version (depuis les releases GitHub) est à venir.

Un journal en bas de la fenêtre affiche la sortie des commandes.

## Compiler

Le launcher est une cible du CMakeLists racine du moteur, comme `LynxEditor`
et `LynxRuntime` : `cmake --build build --target LynxLauncher` (ou la cible
`LynxLauncher` dans CLion). Il utilise le Dear ImGui de `third-party/imgui`
(style pixel compris) et le GLFW précompilé de `third-party/glfw`.

Il ne dépend pas de `lynx.dll` et il est lié en statique (`-static`) : il doit
démarrer sur un ordinateur où MinGW n'est pas encore installé.
Police : `normal-font.ttf` à côté de l'exécutable (comme l'éditeur), sinon Segoe UI.

## Code

```
main.cpp              fenêtre GLFW, boucle ImGui, style, polices
Launcher.*        interface et tâches en arrière-plan (un thread à la fois)
Dependencies.*    liste des dépendances, détection, installation winget
Models.*          catalogue de modèles, ollama list / pull / rm / serve
Process.*         lancement de commandes avec lecture de la sortie, PATH
```

Pour ajouter une dépendance : un bloc dans `Dependencies::CreateList()`.
Pour ajouter un modèle au catalogue : une ligne dans `Models::Catalog()`.
