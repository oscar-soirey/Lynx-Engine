"""Places a row of mushrooms next to the player. Arguments : [count] [spacing]"""

import sys

import lynx_editor as lynx


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    spacing = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0

    players = lynx.actors(cls="Player")
    if not players:
        print("No Player in the level.")
        return

    x, y, z = players[0].location

    # ONE Ctrl+Z removes the whole row.
    with lynx.undo_group("Mushroom row"):
        for i in range(count):
            mushroom = lynx.spawn("Mushroom", (x + spacing * (i + 1), y, z))
            print("spawned", mushroom.id, "at", mushroom.location)

    lynx.log(f"{count} mushrooms placed (Ctrl+Z to remove them)")


if __name__ == "__main__":
    main()
