from PIL import Image
from pathlib import Path
import re

WIDTH = 128
HEIGHT = 128
CHANNELS = 4

# Taille du preview.
# Mets 1 si tu veux un spritesheet final de 1920x128 pour 15 frames.
# Mets 4 pour un preview de 7680x512.
SCALE = 4

input_dir = Path(".")
output_dir = Path("previews")
output_dir.mkdir(exist_ok=True)

# Détecte :
# player_00.rgba
# player_01.rgba
# player_02.rgba
# ...
pattern = re.compile(
    r"^(?P<name>.+)_(?P<id>\d+)\.rgba$",
    re.IGNORECASE
)

groups = {}

# Regroupe les fichiers par nom d'animation
for rgba_file in input_dir.glob("*.rgba"):
    match = pattern.match(rgba_file.name)

    if not match:
        print(f"[IGNORÉ] {rgba_file.name}: nom non reconnu")
        continue

    name = match.group("name")
    frame_id = int(match.group("id"))

    groups.setdefault(name, []).append(
        (frame_id, rgba_file)
    )


for name, frames in groups.items():

    # Trie les frames :
    # 00, 01, 02, 03...
    frames.sort(key=lambda x: x[0])

    images = []

    for frame_id, rgba_file in frames:

        pixels = rgba_file.read_bytes()

        expected_size = WIDTH * HEIGHT * CHANNELS

        if len(pixels) != expected_size:
            print(
                f"[ERREUR] {rgba_file}: "
                f"taille invalide "
                f"({len(pixels)} au lieu de {expected_size})"
            )
            continue

        # Conversion RGBA brut -> image PIL
        image = Image.frombytes(
            "RGBA",
            (WIDTH, HEIGHT),
            pixels
        )

        # Agrandissement pixel art
        if SCALE != 1:
            image = image.resize(
                (
                    WIDTH * SCALE,
                    HEIGHT * SCALE
                ),
                Image.Resampling.NEAREST
            )

        images.append((frame_id, image))

    if not images:
        continue

    frame_width = WIDTH * SCALE
    frame_height = HEIGHT * SCALE

    # Création du spritesheet horizontal
    spritesheet = Image.new(
        "RGBA",
        (
            frame_width * len(images),
            frame_height
        ),
        (0, 0, 0, 0)
    )

    # Place chaque frame côte à côte
    for index, (frame_id, image) in enumerate(images):

        x = index * frame_width

        spritesheet.paste(
            image,
            (x, 0)
        )

    # Exemple :
    # player_00.rgba ... player_14.rgba
    #
    # donnera :
    # previews/player.png
    output_file = output_dir / f"{name}.png"

    spritesheet.save(output_file)

    print(
        f"Créé : {output_file} "
        f"({len(images)} frames, "
        f"{spritesheet.width}x{spritesheet.height})"
    )

print("\nTerminé.")