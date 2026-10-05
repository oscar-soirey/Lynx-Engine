# -*- coding: utf-8 -*-
"""Evaluation cases of Lynxie.

A case = a scene (actors + voxels, always the same), a French request, checks
on the result, and a GOLD solution (the right editor commands, or the right
text answer). `run_eval.py --gold` plays the gold solutions instead of a model :
every case must pass, which validates the checks themselves.

Coordinates are voxel CELLS everywhere here. In gold commands :
    cells(dx, dy)  -> world offset of dx, dy cells       [dx*w, dy*h, 0]
    cell(x, y)     -> world position of the center of a cell
    "$type1"       -> first voxel type of the project ("$type2" : the second)
and "{type1}" / "{type2}" in a request -> the name of that voxel type.
"""

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------

def actor(id, cls, x, y, **props):
    return {"id": id, "class": cls, "cell": [x, y], "properties": props}


def cells(dx, dy):
    return {"$cells": [dx, dy]}


def cell(x, y):
    return {"$cell": [x, y]}


def cmd(_command, **params):
    return {"command": _command, "params": params}


def answer(text):
    """Gold solution of a case answered with text only (no script)."""
    return {"answer": text}


# Checks ----------------------------------------------------------------------

def moved(id, dx, dy):                return {"type": "moved", "id": id, "delta": [dx, dy]}
def at_cell(id, x, y):                return {"type": "at_cell", "id": id, "cell": [x, y]}
def others_unchanged(*except_ids):    return {"type": "others_unchanged", "except": list(except_ids)}
def exists(id, cls=None, x=None, y=None):
    return {"type": "exists", "id": id, "class": cls, "cell": None if x is None else [x, y]}
def absent(id):                       return {"type": "absent", "id": id}
def count(cls, n):                    return {"type": "count", "class": cls, "count": n}
def new_actors(cls, positions, **props):
    return {"type": "new_actors", "class": cls, "cells": [list(p) for p in positions], "properties": props}
def prop(id, name, value):            return {"type": "property", "id": id, "name": name, "value": value}
def prop_contains(id, name, text):    return {"type": "property_contains", "id": id, "name": name, "text": text}
def voxel(x, y, t):                   return {"type": "voxels", "rect": [x, y, x, y], "voxel": t}
def voxels(x0, y0, x1, y1, t):        return {"type": "voxels", "rect": [x0, y0, x1, y1], "voxel": t}
def no_code():                        return {"type": "no_code"}
def asks():                           return {"type": "asks"}
def mentions(*any_of):                return {"type": "mentions", "any": list(any_of)}
def mentions_all(*all_of):            return {"type": "mentions_all", "all": list(all_of)}
def js_class(name, **props):          return {"type": "js_class", "name": name, "properties": props}
def new_file(prefix="", ext=""):      return {"type": "new_file", "prefix": prefix, "ext": ext}


# -----------------------------------------------------------------------------
# Scenes (cells ; the ground is rows y = 0..2 of type 1, from x = 0 to 24)
# -----------------------------------------------------------------------------

GROUND = [{"rect": [0, 0, 24, 2], "voxel": "$type1"}]

SCENES = {
    "basic": {
        "actors": [
            actor("Hero", "EvalHero", 5, 3),
            actor("Enemy", "EvalEnemy", 12, 3),
            actor("Coin", "EvalCoin", 8, 5),
            actor("Crate", "EvalCrate", 3, 3),
            actor("Door", "EvalDoor", 16, 3),
            actor("Torch", "EvalTorch", 10, 7),
        ],
        "voxels": GROUND,
    },
    "two_enemies": {
        "actors": [
            actor("Hero", "EvalHero", 5, 3),
            actor("Enemy", "EvalEnemy", 9, 3),
            actor("Enemy_2", "EvalEnemy", 15, 3),
            actor("Coin", "EvalCoin", 7, 5),
            actor("Coin_2", "EvalCoin", 11, 5),
            actor("Coin_3", "EvalCoin", 13, 5),
        ],
        "voxels": GROUND,
    },
    "crowd": {
        "actors": [
            actor("Hero", "EvalHero", 10, 3),
            actor("Enemy", "EvalEnemy", 4, 3),
            actor("Enemy_2", "EvalEnemy", 14, 3),
            actor("Enemy_3", "EvalEnemy", 18, 3),
            actor("Enemy_4", "EvalEnemy", 22, 3),
            actor("Coin", "EvalCoin", 6, 6),
            actor("Coin_2", "EvalCoin", 12, 6),
            actor("Coin_3", "EvalCoin", 16, 6),
            actor("Torch", "EvalTorch", 2, 8),
            actor("Torch_2", "EvalTorch", 20, 8),
        ],
        "voxels": GROUND,
    },
    "no_hero": {
        "actors": [
            actor("Enemy", "EvalEnemy", 6, 3),
            actor("Coin", "EvalCoin", 9, 5),
            actor("Crate", "EvalCrate", 12, 3),
        ],
        "voxels": GROUND,
    },
    "empty": {
        "actors": [],
        "voxels": GROUND,
    },
}


# -----------------------------------------------------------------------------
# Cases
# -----------------------------------------------------------------------------

def case(id, category, scene, request, checks, gold, select=None, needs_types=1):
    return {
        "id": id, "category": category, "scene": scene, "request": request,
        "checks": checks, "gold": gold, "select": select, "needs_types": needs_types,
    }


def move(id, dx, dy):
    return cmd("actor.set_transform", id=id, location=cells(dx, dy), relative=True)


def place(id, x, y):
    return cmd("actor.set_transform", id=id, location=cell(x, y))


CASES = [
    # --- Move : direction + distance -----------------------------------------
    case("move_right_default", "move", "basic",
         "déplace le personnage vers la droite",
         [moved("Hero", 1, 0), others_unchanged("Hero")],
         [move("Hero", 1, 0)]),
    case("move_left_3", "move", "basic",
         "déplace le joueur de 3 cases vers la gauche",
         [moved("Hero", -3, 0), others_unchanged("Hero")],
         [move("Hero", -3, 0)]),
    case("move_up_2", "move", "basic",
         "monte le perso de 2 cases",
         [moved("Hero", 0, 2), others_unchanged("Hero")],
         [move("Hero", 0, 2)]),
    case("move_down_1", "move", "basic",
         "descends le héros d'une case",
         [moved("Hero", 0, -1), others_unchanged("Hero")],
         [move("Hero", 0, -1)]),
    case("move_diagonal", "move", "basic",
         "bouge le personnage de 2 cases à droite et 1 vers le haut",
         [moved("Hero", 2, 1), others_unchanged("Hero")],
         [move("Hero", 2, 1)]),
    case("move_un_peu", "move", "basic",
         "décale un peu le joueur vers la droite",
         [moved("Hero", 1, 0), others_unchanged("Hero")],
         [move("Hero", 1, 0)]),
    case("move_english", "move", "basic",
         "move the player 5 cells to the right",
         [moved("Hero", 5, 0), others_unchanged("Hero")],
         [move("Hero", 5, 0)]),
    case("move_enemy_single", "move", "basic",
         "pousse l'ennemi de 4 cases vers la gauche",
         [moved("Enemy", -4, 0), others_unchanged("Enemy")],
         [move("Enemy", -4, 0)]),
    case("move_crate", "move", "basic",
         "décale la caisse de 2 cases à droite",
         [moved("Crate", 2, 0), others_unchanged("Crate")],
         [move("Crate", 2, 0)]),
    case("move_torch_up", "move", "basic",
         "monte la torche de 3 blocs",
         [moved("Torch", 0, 3), others_unchanged("Torch")],
         [move("Torch", 0, 3)]),
    case("move_door_left", "move", "basic",
         "déplace la porte d'un bloc vers la gauche",
         [moved("Door", -1, 0), others_unchanged("Door")],
         [move("Door", -1, 0)]),
    case("move_coin_down", "move", "basic",
         "fais descendre la pièce de 2 cases",
         [moved("Coin", 0, -2), others_unchanged("Coin")],
         [move("Coin", 0, -2)]),
    case("move_selected", "move", "basic",
         "déplace l'acteur sélectionné de 2 cases vers le haut",
         [moved("Crate", 0, 2), others_unchanged("Crate")],
         [move("Crate", 0, 2)], select="Crate"),
    case("move_it_selected", "move", "basic",
         "bouge-le d'une case à gauche",
         [moved("Torch", -1, 0), others_unchanged("Torch")],
         [move("Torch", -1, 0)], select="Torch"),

    # --- Absolute placement ----------------------------------------------------
    case("place_cell", "place", "basic",
         "mets le joueur à la case 10, 3",
         [at_cell("Hero", 10, 3), others_unchanged("Hero")],
         [place("Hero", 10, 3)]),
    case("place_on_coin", "place", "basic",
         "téléporte le joueur sur la pièce",
         [at_cell("Hero", 8, 5), others_unchanged("Hero")],
         [place("Hero", 8, 5)]),
    case("place_left_of_door", "place", "basic",
         "place le joueur juste à gauche de la porte",
         [at_cell("Hero", 15, 3), others_unchanged("Hero")],
         [place("Hero", 15, 3)]),
    case("swap_hero_enemy", "place", "basic",
         "échange les positions du joueur et de l'ennemi",
         [at_cell("Hero", 12, 3), at_cell("Enemy", 5, 3), others_unchanged("Hero", "Enemy")],
         [place("Hero", 12, 3), place("Enemy", 5, 3)]),
    case("align_coins", "place", "two_enemies",
         "mets toutes les pièces à la même hauteur que le joueur, sans changer leur x",
         [at_cell("Coin", 7, 3), at_cell("Coin_2", 11, 3), at_cell("Coin_3", 13, 3),
          others_unchanged("Coin", "Coin_2", "Coin_3")],
         [place("Coin", 7, 3), place("Coin_2", 11, 3), place("Coin_3", 13, 3)]),

    # --- Choosing the target ---------------------------------------------------
    case("all_enemies_up", "target", "two_enemies",
         "monte tous les ennemis de 2 cases",
         [moved("Enemy", 0, 2), moved("Enemy_2", 0, 2), others_unchanged("Enemy", "Enemy_2")],
         [move("Enemy", 0, 2), move("Enemy_2", 0, 2)]),
    case("ambiguous_enemy", "target", "two_enemies",
         "déplace l'ennemi vers la droite",
         [asks(), others_unchanged()],
         answer("Il y a deux ennemis (Enemy en case [9, 3] et Enemy_2 en case [15, 3]) : lequel veux-tu déplacer ?")),
    case("enemy_by_id", "target", "two_enemies",
         "déplace Enemy_2 de 3 cases vers la gauche",
         [moved("Enemy_2", -3, 0), others_unchanged("Enemy_2")],
         [move("Enemy_2", -3, 0)]),
    case("enemy_on_the_right", "target", "two_enemies",
         "déplace l'ennemi de droite d'une case vers le haut",
         [moved("Enemy_2", 0, 1), others_unchanged("Enemy_2")],
         [move("Enemy_2", 0, 1)]),
    case("nearest_enemy", "target", "crowd",
         "déplace l'ennemi le plus proche du joueur d'une case vers la droite",
         [moved("Enemy_2", 1, 0), others_unchanged("Enemy_2")],
         [move("Enemy_2", 1, 0)]),
    case("all_coins_right", "target", "two_enemies",
         "décale toutes les pièces de 2 cases à droite",
         [moved("Coin", 2, 0), moved("Coin_2", 2, 0), moved("Coin_3", 2, 0),
          others_unchanged("Coin", "Coin_2", "Coin_3")],
         [move("Coin", 2, 0), move("Coin_2", 2, 0), move("Coin_3", 2, 0)]),

    # --- Spawn -----------------------------------------------------------------
    case("spawn_enemy_right_of_hero", "spawn", "basic",
         "ajoute un ennemi 3 cases à droite du joueur",
         [new_actors("EvalEnemy", [(8, 3)]), count("EvalEnemy", 2), others_unchanged()],
         [cmd("actor.spawn", **{"class": "EvalEnemy", "voxel": [8, 3]})]),
    case("spawn_coin_line", "spawn", "basic",
         "ajoute 5 pièces en ligne sur la rangée y = 9, de x = 2 à x = 6",
         [new_actors("EvalCoin", [(x, 9) for x in range(2, 7)]), others_unchanged()],
         [cmd("actor.spawn", **{"class": "EvalCoin", "voxel": [x, 9]}) for x in range(2, 7)]),
    case("spawn_torch_above_door", "spawn", "basic",
         "mets une nouvelle torche 2 cases au-dessus de la porte",
         [new_actors("EvalTorch", [(16, 5)]), others_unchanged()],
         [cmd("actor.spawn", **{"class": "EvalTorch", "voxel": [16, 5]})]),
    case("spawn_crate_at", "spawn", "basic",
         "crée une caisse en case 20, 3",
         [new_actors("EvalCrate", [(20, 3)]), others_unchanged()],
         [cmd("actor.spawn", **{"class": "EvalCrate", "voxel": [20, 3]})]),
    case("spawn_named_boss", "spawn", "basic",
         "ajoute un ennemi nommé Boss en case 18, 3",
         [exists("Boss", "EvalEnemy", 18, 3), others_unchanged()],
         [cmd("actor.spawn", **{"class": "EvalEnemy", "id": "Boss", "voxel": [18, 3]})]),
    case("spawn_with_property", "spawn", "basic",
         "ajoute un ennemi avec 200 PV en case 20, 3",
         [new_actors("EvalEnemy", [(20, 3)], hp=200), others_unchanged()],
         [cmd("actor.spawn", **{"class": "EvalEnemy", "voxel": [20, 3], "properties": {"hp": 200}})]),
    case("spawn_crate_grid", "spawn", "empty",
         "place 6 caisses en grille de 3 colonnes sur 2 rangées, la première en case 4, 3, "
         "les autres vers la droite et vers le haut",
         [new_actors("EvalCrate", [(4, 3), (5, 3), (6, 3), (4, 4), (5, 4), (6, 4)])],
         [cmd("actor.spawn", **{"class": "EvalCrate", "voxel": [x, y]}) for y in (3, 4) for x in (4, 5, 6)]),

    # --- Delete ----------------------------------------------------------------
    case("delete_coin", "delete", "basic",
         "supprime la pièce",
         [absent("Coin"), others_unchanged("Coin")],
         [cmd("actor.delete", id="Coin")]),
    case("delete_torch", "delete", "basic",
         "enlève la torche",
         [absent("Torch"), others_unchanged("Torch")],
         [cmd("actor.delete", id="Torch")]),
    case("delete_all_coins", "delete", "two_enemies",
         "supprime toutes les pièces",
         [count("EvalCoin", 0), others_unchanged("Coin", "Coin_2", "Coin_3")],
         [cmd("actor.delete", ids=["Coin", "Coin_2", "Coin_3"])]),
    case("delete_leftmost_enemy", "delete", "crowd",
         "supprime l'ennemi le plus à gauche",
         [absent("Enemy"), count("EvalEnemy", 3), others_unchanged("Enemy")],
         [cmd("actor.delete", id="Enemy")]),
    case("delete_enemies_right_of_hero", "delete", "crowd",
         "supprime les ennemis qui sont à droite du joueur",
         [absent("Enemy_2"), absent("Enemy_3"), absent("Enemy_4"), exists("Enemy"),
          others_unchanged("Enemy_2", "Enemy_3", "Enemy_4")],
         [cmd("actor.delete", ids=["Enemy_2", "Enemy_3", "Enemy_4"])]),

    # --- Properties ------------------------------------------------------------
    case("set_hero_hp", "property", "basic",
         "mets les PV du joueur à 50",
         [prop("Hero", "hp", 50), others_unchanged()],
         [cmd("actor.set_property", id="Hero", name="hp", value=50)]),
    case("double_enemy_speed", "property", "two_enemies",
         "double la vitesse des ennemis",
         [prop("Enemy", "speed", 2.0), prop("Enemy_2", "speed", 2.0), others_unchanged()],
         [cmd("actor.set_property", id="Enemy", name="speed", value=2.0),
          cmd("actor.set_property", id="Enemy_2", name="speed", value=2.0)]),
    case("open_door", "property", "basic",
         "ouvre la porte",
         [prop("Door", "open", True), others_unchanged()],
         [cmd("actor.set_property", id="Door", name="open", value=True)]),
    case("unlock_door", "property", "basic",
         "déverrouille la porte",
         [prop("Door", "locked", False), others_unchanged()],
         [cmd("actor.set_property", id="Door", name="locked", value=False)]),
    case("torch_radius", "property", "basic",
         "passe le rayon de la torche à 8",
         [prop("Torch", "radius", 8.0), others_unchanged()],
         [cmd("actor.set_property", id="Torch", name="radius", value=8.0)]),
    case("coins_value_10", "property", "two_enemies",
         "chaque pièce doit valoir 10",
         [prop("Coin", "value", 10), prop("Coin_2", "value", 10), prop("Coin_3", "value", 10), others_unchanged()],
         [cmd("actor.set_property", id=i, name="value", value=10) for i in ("Coin", "Coin_2", "Coin_3")]),
    case("enemy_damage_plus_3", "property", "basic",
         "augmente les dégâts de l'ennemi de 3",
         [prop("Enemy", "damage", 8), others_unchanged()],
         [cmd("actor.set_property", id="Enemy", name="damage", value=8)]),
    case("rename_hero", "property", "basic",
         "renomme le joueur en Bob",
         [absent("Hero"), exists("Bob", "EvalHero", 5, 3), others_unchanged("Hero")],
         [cmd("actor.rename", id="Hero", new_id="Bob")]),

    # --- Duplicate -------------------------------------------------------------
    case("duplicate_crate_up", "duplicate", "basic",
         "duplique la caisse 3 cases au-dessus",
         [new_actors("EvalCrate", [(3, 6)]), others_unchanged()],
         [cmd("actor.duplicate", id="Crate", offset=cells(0, 3))]),
    case("three_enemy_copies", "duplicate", "basic",
         "fais 3 copies de l'ennemi, chacune une case plus à droite que la précédente",
         [new_actors("EvalEnemy", [(13, 3), (14, 3), (15, 3)]), others_unchanged()],
         [cmd("actor.duplicate", id="Enemy", offset=cells(i, 0)) for i in (1, 2, 3)]),

    # --- Voxels ----------------------------------------------------------------
    case("fill_rect_type2", "voxel", "basic",
         "remplis le rectangle de la case (0, 10) à la case (5, 12) avec du {type2}",
         [voxels(0, 10, 5, 12, "$type2"), others_unchanged()],
         [cmd("voxel.fill", x0=0, y0=10, x1=5, y1=12, type="$type2")], needs_types=2),
    case("dig_under_hero", "voxel", "basic",
         "creuse le sol juste sous le joueur (une seule case)",
         [voxel(5, 2, 0), voxel(4, 2, "$type1"), voxel(6, 2, "$type1"), voxel(5, 1, "$type1"), others_unchanged()],
         [cmd("voxel.set", x=5, y=2, type=0)]),
    case("wall_right_of_hero", "voxel", "basic",
         "construis un mur de {type1} de 4 cases de haut juste à droite du joueur, en partant du sol",
         [voxels(6, 3, 6, 6, "$type1"), voxel(6, 7, 0), voxel(7, 3, 0), others_unchanged()],
         [cmd("voxel.fill", x0=6, y0=3, x1=6, y1=6, type="$type1")]),
    case("clear_ground_left", "voxel", "basic",
         "efface tous les voxels de x = 0 à x = 4 sur les rangées y = 0 à y = 2",
         [voxels(0, 0, 4, 2, 0), voxels(5, 0, 6, 2, "$type1"), others_unchanged()],
         [cmd("voxel.fill", x0=0, y0=0, x1=4, y1=2, type=0)]),
    case("platform_above_enemy", "voxel", "basic",
         "fais une plateforme horizontale de 5 blocs de {type1}, 3 cases au-dessus de l'ennemi et centrée sur lui",
         [voxels(10, 6, 14, 6, "$type1"), voxel(9, 6, 0), voxel(15, 6, 0), others_unchanged()],
         [cmd("voxel.fill", x0=10, y0=6, x1=14, y1=6, type="$type1")]),

    # --- Questions (answer from the scene state, no script) ---------------------
    case("where_is_hero", "question", "basic",
         "où est le joueur ?",
         [no_code(), mentions_all("5", "3"), others_unchanged()],
         answer("Le joueur (Hero) est en case [5, 3].")),
    case("how_many_enemies", "question", "crowd",
         "combien y a-t-il d'ennemis dans le niveau ?",
         [no_code(), mentions("4", "quatre"), others_unchanged()],
         answer("Il y a 4 ennemis : Enemy, Enemy_2, Enemy_3 et Enemy_4.")),
    case("nearest_enemy_question", "question", "crowd",
         "quel ennemi est le plus proche du joueur ?",
         [no_code(), mentions("Enemy_2"), others_unchanged()],
         answer("Le plus proche est Enemy_2, en case [14, 3] (4 cases à droite du joueur).")),

    # --- Must ask / refuse (no clear target) ------------------------------------
    case("no_hero_move", "ask", "no_hero",
         "déplace le joueur vers la droite",
         [asks(), others_unchanged()],
         answer("Je ne trouve pas de joueur dans le niveau (il y a Enemy, Coin et Crate). Quel acteur veux-tu déplacer ?")),
    case("unknown_dragon", "ask", "basic",
         "fais avancer le dragon de 2 cases",
         [no_code(), others_unchanged()],
         answer("Il n'y a pas de dragon dans le niveau (acteurs : Hero, Enemy, Coin, Crate, Door, Torch). Lequel veux-tu déplacer ?")),
    case("move_it_nothing_selected", "ask", "basic",
         "déplace-le de 2 cases",
         [asks(), others_unchanged()],
         answer("Aucun acteur n'est sélectionné : lequel veux-tu déplacer, et dans quelle direction ?")),

    # --- JavaScript ------------------------------------------------------------
    case("js_class_bat", "javascript", "basic",
         "crée une classe JavaScript EvalBat qui hérite d'Actor, avec une propriété speed qui vaut 3",
         [js_class("EvalBat", speed=3.0), new_file(ext=".js"), others_unchanged()],
         [cmd("asset.write", path="EvalBat.js",
              content="class EvalBat extends Actor {\n    static properties = {\n        speed: 3.0,\n    };\n}\n")]),
    case("js_class_in_folder", "javascript", "basic",
         "crée une classe JS EvalSlime dans le dossier monstres/ : c'est un ennemi, elle hérite de EvalEnemy",
         [js_class("EvalSlime"), new_file(prefix="monstres/", ext=".js"), others_unchanged()],
         [cmd("asset.write", path="monstres/EvalSlime.js",
              content="class EvalSlime extends EvalEnemy {\n}\n")]),
    case("js_script_attach", "javascript", "basic",
         "écris un script JS qui fait tourner le joueur sur lui-même et attache-le au joueur",
         [prop_contains("Hero", "scripts", ".js"), new_file(ext=".js"), others_unchanged()],
         [cmd("asset.write", path="scripts/spin.js",
              content="function Update(dt) {\n    parent.transform.rotation.z += 90 * dt;\n}\n"),
          cmd("actor.set_property", id="Hero", name="scripts", value="scripts/spin.js")]),
]
