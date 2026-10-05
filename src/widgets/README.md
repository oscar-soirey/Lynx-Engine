# Widgets (UI, type UMG)

En JavaScript : `UI.create(...)`, `class X extends UserWidget` (voir `scripting/README.md`).

Arbre de widgets fait dans le **Widget Editor** (fichiers `.widget` dans
`assets/`) ou en code. Aucun type HRL dans les en-têtes : le rendu est dans
les `.cpp` (`Private/`).

```cpp
#include <Lynx.h>

auto* hud = lynx::CreateWidget(player, "ui/Hud.widget");   // player : nullptr = joueur 0
hud->GetWidget<lynx::ProgressBar>("Health")->percent = 0.75f;   // vu à la frame suivante
hud->GetWidget<lynx::Button>("Pause")->OnClicked.Subscribe([] { /* ... */ });
hud->AddToViewport();          // viewport du joueur (split-screen compris)
hud->RemoveFromParent();       // retiré de l'écran, toujours utilisable
lynx::DestroyWidget(hud);      // détruit
```

Classe C++ (comme un Widget Blueprint avec du code) : dériver de
`lynx::UserWidget`, surcharger `NativeConstruct` / `NativeTick` /
`NativeDestruct`, l'enregistrer avec `LYNX_MODULE_REGISTER`, et mettre son nom
dans *Class* du Widget Editor (ou `lynx::CreateWidget<MaClasse>(player, path)`).
Le bouton *Copy C++ class* du Widget Editor génère le squelette.

| Panels | Widgets |
|---|---|
| `CanvasPanel` (ancres, offsets, alignement, z-order) | `TextBlock`, `Button`, `Image` |
| `VerticalBox`, `HorizontalBox` (Auto / Fill, padding, alignement) | `Slider`, `CheckBox`, `ProgressBar` |
| `Overlay`, `Border` (fond + un enfant) | `Spacer`, `UserWidget` (un autre `.widget`) |

- **Unités** : « slate units ». Un UserWidget remplit le viewport de son joueur ;
  DPI scale = petit côté du viewport / 1080 (`lynx::ui::SetDPIReference`). Une
  interface faite en 1920 x 1080 garde ses proportions à toute résolution.
- **Visibilité** : Visible, Collapsed (pas de place), Hidden (garde sa place),
  HitTestInvisible, SelfHitTestInvisible.
- **Événements** (clic, slider, checkbox, survol) : envoyés au début de la
  frame suivante, jamais pendant le rendu : un callback peut détruire n'importe
  quel widget.
- **Durée de vie** : les widgets créés par `CreateWidget` appartiennent au
  moteur et sont tous détruits à la fin du jeu (`EndGame`) et avec le niveau.
- Les hôtes (éditeur, runtime) donnent la taille de rendu avec
  `Engine::SetRenderSize(w, h)`.
