from PIL import Image
from pathlib import Path

WIDTH = 128
HEIGHT = 128
CHANNELS = 4

input_dir = Path(".")
output_dir = Path("previews")
output_dir.mkdir(exist_ok=True)

for rgba_file in input_dir.glob("*.rgba"):
    pixels = rgba_file.read_bytes()

    expected_size = WIDTH * HEIGHT * CHANNELS

    if len(pixels) != expected_size:
        print(f"[ERREUR] {rgba_file}: taille invalide")
        continue

    image = Image.frombytes(
        "RGBA",
        (WIDTH, HEIGHT),
        pixels
    )

    # Agrandissement pour visualiser le pixel art
    scale = 4

    preview = image.resize(
        (WIDTH * scale, HEIGHT * scale),
        Image.Resampling.NEAREST
    )

    output_file = output_dir / f"{rgba_file.stem}.png"
    preview.save(output_file)

    print(f"Créé : {output_file}")