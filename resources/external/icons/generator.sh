#!/bin/bash

# Print instructions only; do not expand or execute the commands below.
cat <<'EOF'
Run these commands from resources/external/icons.

1. Generate PNGs at each required size:
dnf install ImageMagick
for s in 512 256 128 64 48 32 16; do
    mkdir -p "${s}x${s}"
    magick -background none notquiterss-lighting-pro.svg \
         -filter Lanczos -resize "${s}x${s}" -extent "${s}x${s}"\
         -gravity center -depth 8 \
         PNG32:"${s}x${s}/notquiterss.png"
done

2. Compress the generated PNGs:
dnf install oxipng
for s in 512 256 128 64 48 32 16; do
    oxipng -p -o max "${s}x${s}/notquiterss.png"
done

3. Generate the macOS icon:
dnf install libicns-utils
png2icns application.icns 16x16/notquiterss.png 32x32/notquiterss.png \
    48x48/notquiterss.png 128x128/notquiterss.png \
    256x256/notquiterss.png 512x512/notquiterss.png

4. Generate the Windows icon:
magick 16x16/notquiterss.png 32x32/notquiterss.png 48x48/notquiterss.png \
    64x64/notquiterss.png 128x128/notquiterss.png \
    256x256/notquiterss.png application.ico
EOF
