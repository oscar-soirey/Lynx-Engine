# Blocky

Jeu pour le moteur Lynx. Ce dossier peut être n'importe où sur l'ordinateur.

```
blocky/
  assets/       assets du jeu (voxels.json, world.xml, sons, sprites...)
  src/          code du jeu -> Blocky.dll
  build/        Blocky.dll (là où l'éditeur et le runtime la cherchent)
  input.json    touches (fenêtre Input Settings de l'éditeur)
```

## Compiler

1. Configurer le moteur une fois (ouvrir le projet du moteur dans CLion, ou
   `cmake` dessus) : il s'enregistre dans le registre CMake de l'ordinateur.
2. Ici : `Build.bat`, ou ouvrir ce dossier dans CLion et compiler la cible `Blocky`.

Pour utiliser un autre build du moteur : `set LYNX_DIR=C:\...\lynx\<dossier de build>`
avant `GenerateProjectFiles.bat`, ou `-DLynx_DIR=...` dans les options CMake de CLion.

Le jeu doit être compilé avec le même compilateur (MinGW) que le moteur.
Après une modification du moteur, recompiler aussi le jeu.

## Lancer

- Éditeur : `LynxEditor.exe`, puis ouvrir ce dossier (ou `LynxEditor.exe "C:\...\blocky"`).
- Jeu seul : `LynxRuntime.exe "C:\...\blocky"`.
