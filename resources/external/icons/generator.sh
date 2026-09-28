#! /bin/bash

cat << EOF
Use shell, I will not run anything

1. Icons for different sizes ...
dnf install GraphicsMagick
for s in 512 256 128 64 32 16; do gm convert notquiterss.png -filter Lanczos -resize $((s))x$((s)) -background none -gravity center ${s}x{s}/notquiterss.png; done

2. Compress PNGs
for i in *.png; do echo "Processing '$i' ..."; oxipng -p -o max "$i"; done

3. Generate a MacOS iconset:
dnf install libicns-utils
png2icns application.icns $(printf '%s\n' [0-9]*x[0-9]*/ | grep -vE '(64x64|24x24)' | sort -rV | sed 's|$|notquiterss.png|')

4. Generate a Windows icon:
dnf install ImageMagick
magick $(printf '%s\n' [0-9]*x[0-9]*/ | sort -rV | sed 's|$|*|') favicon.ico
EOF
