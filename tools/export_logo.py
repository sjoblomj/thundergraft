#!/usr/bin/env python3
"""Exports src/thundergraft.svg to src/thundergraft.bmp, the logo in the
settings dialog. Run it after changing the SVG, and commit both files.

A static control in a dialog can only show a bitmap. The BMP is committed so
that building the plugin needs no SVG tools. This script needs rsvg-convert
(librsvg) and Pillow.
"""

import io
import pathlib
import subprocess

from PIL import Image

# Fits the logo control, IDC_LOGO in ThunderGraft.rc
WIDTH = 480
# The bitmap has no transparency, so the logo is flattened onto this colour.
# It should match the SVG's background: SS_CENTERIMAGE fills any space around
# the bitmap with the colour of its top left pixel.
BACKGROUND = "#040404"

SRC = pathlib.Path(__file__).resolve().parent.parent / "src"


def main():
    png = subprocess.run(
        [
            "rsvg-convert",
            f"--width={WIDTH}",
            f"--background-color={BACKGROUND}",
            str(SRC / "thundergraft.svg"),
        ],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout
    image = Image.open(io.BytesIO(png)).convert("RGB")
    image.save(SRC / "thundergraft.bmp")
    print(f"src/thundergraft.bmp: {image.width}x{image.height}")


if __name__ == "__main__":
    main()
