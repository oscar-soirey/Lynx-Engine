"""Lists the actors of the level, grouped by class."""

import collections

import lynx_editor as lynx


def main():
    info = lynx.info()
    print(f"{info['project']} : {info['actor_count']} actors")

    by_class = collections.defaultdict(list)
    for actor in lynx.actors():
        by_class[actor.cls].append(actor)

    for cls in sorted(by_class):
        print(f"\n{cls} ({len(by_class[cls])})")
        for actor in by_class[cls]:
            x, y, _ = actor.location
            print(f"   {actor.id:<24} x={x:9.2f}  y={y:9.2f}")


if __name__ == "__main__":
    main()
