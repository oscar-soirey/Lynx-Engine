"""Builds a voxel platform under the player. Arguments : [width] [voxel type name]"""

import sys

import lynx_editor as lynx


def main():
    width = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    types = lynx.voxels.types()
    type_name = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else types[0]["name"]

    print("voxel types :", ", ".join(f"{t['type']}={t['name']}" for t in types))

    players = lynx.actors(cls="Player")
    if not players:
        print("No Player in the level.")
        return

    x, y, _ = players[0].location
    cell_x, cell_y = lynx.voxels.world_to_cell(x, y)

    # 3 cells under the player, `width` cells wide, 2 cells thick.
    top = cell_y - 3
    changed = lynx.voxels.fill(cell_x - width // 2, top - 1, cell_x + width // 2, top, type_name)
    print(f"platform of {type_name} : {changed} voxels changed (one Ctrl+Z)")


if __name__ == "__main__":
    main()
