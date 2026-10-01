"""Renders the alternate Nagram icons from their Icon Composer sources.

Run from the repository root on macOS with Xcode 26 or later:
    uv run --no-project --with pillow python \
        Telegram/Resources/branding/export_icons.py
"""

import io
import pathlib
import subprocess
import sys
import tempfile

from PIL import Image, ImageCms

ICTOOL = pathlib.Path(
    '/Applications/Xcode.app/Contents/Applications/Icon Composer.app'
    '/Contents/Executables/ictool')
SIZE = 512
ICONS = {
    'NagramBlock': 'block',
    'NagramBlockBlack': 'block_black',
    'NagramBlockBlue': 'block_blue',
    'NagramBlockNiello': 'block_niello',
    'NagramBlockPurple': 'block_purple',
    'NagramClassic': 'classic',
    'NagramColorful': 'colorful',
    'NagramCyan': 'cyan',
    'NagramBlack': 'black',
}
RENDITIONS = {'Default': '', 'Dark': '_dark'}


def render(source, rendition, target):
    with tempfile.TemporaryDirectory() as directory:
        raw = pathlib.Path(directory) / 'icon.png'
        subprocess.run([
            str(ICTOOL), str(source), '--export-image',
            '--output-file', str(raw),
            '--platform', 'macOS',
            '--rendition', rendition,
            '--width', str(SIZE),
            '--height', str(SIZE),
            '--scale', '1',
        ], check=True, stdout=subprocess.DEVNULL)
        image = Image.open(raw)
        profile = image.info.get('icc_profile')
        image = image.convert('RGBA')
        if profile:
            image = ImageCms.profileToProfile(
                image,
                ImageCms.ImageCmsProfile(io.BytesIO(profile)),
                ImageCms.createProfile('sRGB'),
                outputMode='RGBA')
        image.save(target, optimize=True)


def main():
    if not ICTOOL.exists():
        sys.exit(f'ictool not found at {ICTOOL}')
    branding = pathlib.Path(__file__).resolve().parent
    output = branding.parent / 'nagram' / 'icons'
    output.mkdir(parents=True, exist_ok=True)
    for name, identifier in ICONS.items():
        source = branding / f'{name}.icon'
        if not source.is_dir():
            sys.exit(f'missing icon source {source}')
        for rendition, suffix in RENDITIONS.items():
            target = output / f'{identifier}{suffix}.png'
            render(source, rendition, target)
            print(target.relative_to(branding.parent))


if __name__ == '__main__':
    main()
