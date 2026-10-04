"""Saves an image of the scene in .lynx/screenshots/."""

import datetime
import pathlib

import lynx_editor as lynx


def main():
    folder = pathlib.Path(".lynx") / "screenshots"
    path = folder / datetime.datetime.now().strftime("scene_%Y%m%d_%H%M%S.png")
    data = lynx.screenshot(str(path), target="viewport", max_size=0)
    print(f"{path} ({len(data) // 1024} KB)")


if __name__ == "__main__":
    main()
