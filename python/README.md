# Commandes de l'éditeur (Python / IA)

L'éditeur Lynx expose des **commandes** (déplacer, créer, supprimer des acteurs,
peindre des voxels, lire / écrire les fichiers d'`assets/`, compiler le jeu,
capturer l'écran…). On les appelle :

- depuis des **scripts Python** du projet (`<projet>/commands/*.py`, fenêtre
  *Commands > Scripts*) ou n'importe quel script Python externe ;
- depuis la **console** de l'éditeur (*Commands > Console*) ;
- depuis un **modèle d'IA** via le serveur **MCP** `lynx_mcp.py` (Claude Desktop,
  Claude Code ou tout client MCP) : chaque commande devient un outil.

La liste complète des commandes, avec leurs paramètres, est dans l'onglet
*Commands > Reference* (ou `lynx.help()`).

Ce dossier `python/` est copié à côté de `LynxEditor.exe` à la compilation.
Il ne demande que Python 3.8+ (bibliothèque standard uniquement).


## Scripts Python

```python
"""Description affichée dans l'éditeur (survol)."""
import lynx_editor as lynx

# Acteurs
for enemy in lynx.actors(cls="Enemy"):          # filtres : cls, id="Coin_*", near=(x, y), radius
    enemy.move(0, 2)                            # déplacement relatif
    enemy.location = (10, 4, 0)                 # absolu

coin = lynx.spawn("Mushroom", (12, 5, 0), properties={"speed": 3})
coin["speed"] = 5                               # propriété (HPROPERTY / static properties JS)
print(coin["speed"], coin.details()["functions"])
coin.call("Jump")                               # HFUNCTION / méthode JS
coin.duplicate(offset=(4, 0, 0))
coin.delete()

# Voxels (cellules entières, type 0 = vide)
grass = lynx.voxels.type_id("Vert sature")      # nom dans assets/voxels.json
lynx.voxels.fill(0, 0, 40, 2, grass)            # rectangle (outline=True : bord)
lynx.voxels.set(5, 3, 0)
cx, cy = lynx.voxels.world_to_cell(*coin.location[:2])

# Fichiers (assets/ ; lynx.project_files pour la racine du projet : src/, input.json...)
lynx.assets.write("scripts/hello.js", "console.log('hi');")   # .js et voxels.json rechargés
text = lynx.assets.read("voxels.json")
lynx.assets.list("sprite", pattern="*.png", recursive=True)
lynx.assets.delete("old.png")                   # va dans .lynx/trash/ (récupérable)

# Éditeur
lynx.save()                                     # Ctrl+S
lynx.undo()                                     # Ctrl+Z
lynx.screenshot("shot.png")                     # image de la scène
lynx.play(); lynx.wait_frames(120); lynx.stop()
result = lynx.compile()                         # compile le jeu, renvoie les erreurs
lynx.eval_js("Level.find('Pawn').id")           # JavaScript dans le moteur
```

**Annuler** : chaque commande est une étape de Ctrl+Z. Pour en faire une seule :

```python
with lynx.undo_group("Construire le niveau"):
    ...

with lynx.batch() as b:                         # + rapide : une seule requête
    for x in range(1000):
        b.call("actor.spawn", {"class": "Mushroom", "location": [x, 5, 0]})
```

Les modifications ne sont **pas sauvegardées** tant qu'on n'appelle pas
`lynx.save()` (ou Ctrl+S). En Play, elles sont temporaires (le niveau revient à
son état d'avant Play au Stop).

Erreurs : `lynx.LynxError` (acteur inconnu, mauvais paramètre…).

### Lancer un script

- Depuis l'éditeur : *Windows > Commands (Python / AI)*, onglet *Scripts*,
  double-clic ou *Run* (arguments dans le champ à côté). Le champ *Python*
  indique l'exécutable (`python`, `py`, ou un chemin complet).
- Depuis un terminal (l'éditeur ouvert sur le projet) :
  `set PYTHONPATH=C:\...\LynxEditor\python` puis `python commands\mon_script.py`.
  Le script trouve l'éditeur grâce à `<projet>/.lynx/editor.json`.


## IA : serveur MCP

`lynx_mcp.py` transforme chaque commande en outil MCP (`actor.spawn` →
`actor_spawn`…). L'onglet *Commands > AI / MCP* affiche la configuration exacte
à copier.

**Claude Desktop** (`claude_desktop_config.json`) :

```json
{
  "mcpServers": {
    "lynx": {
      "command": "python",
      "args": ["C:/chemin/vers/LynxEditor/python/lynx_mcp.py"]
    }
  }
}
```

**Claude Code** : `claude mcp add lynx -- python C:/chemin/vers/LynxEditor/python/lynx_mcp.py`

Sans `--project`, le serveur MCP se connecte au dernier éditeur lancé (il se
reconnecte tout seul si l'éditeur redémarre). `"args": [".../lynx_mcp.py",
"--project", "C:/Jeux/Blocky"]` fixe un projet.

Le modèle peut alors : lister les acteurs, en créer, les déplacer, modifier leurs
propriétés, peindre des voxels, écrire des scripts JS / des fichiers de
configuration, modifier le code C++ du jeu (`root: "project"`) puis le compiler
avec `editor_compile` (les erreurs du compilateur lui sont renvoyées), lancer le
jeu, et vérifier son travail avec `editor_screenshot` (image).


## Protocole (pour un autre langage)

TCP local `127.0.0.1:<port>`, une ligne JSON par message :

```
-> {"id": 1, "command": "auth", "params": {"token": "<token>"}}
<- {"id": 1, "ok": true, "result": {"client": 1}}
-> {"id": 2, "command": "actor.spawn", "params": {"class": "Mushroom", "location": [3, 4, 0]}}
<- {"id": 2, "ok": true, "result": {"id": "Mushroom_2", ...}}
<- {"id": 3, "ok": false, "error": "unknown actor 'Foo'"}
```

Port et jeton : `<projet>/.lynx/editor.json` (écrit au démarrage de l'éditeur,
supprimé à sa fermeture) ; variables `LYNX_EDITOR_PORT` / `LYNX_EDITOR_TOKEN`
pour les scripts lancés par l'éditeur. Le serveur n'écoute que sur la machine
locale et refuse toute commande avant `auth`.

Sécurité des fichiers : les chemins sont relatifs à `assets/` (ou au projet) ;
`..` qui sort du dossier et les chemins absolus sont refusés ; `build/`, `.lynx/`
et `.git/` sont en lecture seule. Tout fichier remplacé ou supprimé est d'abord
copié dans `.lynx/trash/<date>/`.

Pensez à ajouter `.lynx/` au `.gitignore` du projet.


## Ajouter une commande (C++)

Dans `src/editor/EditorCommands.inl`, fonction `RegisterEditorCommands()` :

```cpp
cmd::Register("actor.count",
    "Number of actors of a class.",
    { { "class", "string", "Class name.", true } },
    [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
    {
        const std::string name = cmd::GetString(params, "class");
        return { { "count", CmdLevel()->CountActorsOfClass(name.c_str()) } };
    });
```

Elle apparaît automatiquement dans la référence, la console, Python
(`lynx.call("actor.count", {"class": "Enemy"})`) et le serveur MCP.
