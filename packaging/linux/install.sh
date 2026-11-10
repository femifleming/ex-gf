#!/bin/bash
# EX-GF installer for Linux: VST3 -> ~/.vst3, LV2 -> ~/.lv2, apps -> ~/.local/bin (+ desktop entries)
cd "$(dirname "$0")"
set -e
mkdir -p ~/.vst3 ~/.lv2 ~/.local/bin ~/.local/share/applications
for f in vst3/*.vst3; do rm -rf ~/.vst3/"$(basename "$f")"; cp -r "$f" ~/.vst3/; done
for f in lv2/*.lv2; do rm -rf ~/.lv2/"$(basename "$f")"; cp -r "$f" ~/.lv2/; done
for f in bin/*; do
  n="$(basename "$f")"; cp "$f" ~/.local/bin/"$n"; chmod +x ~/.local/bin/"$n"
  printf '[Desktop Entry]\nType=Application\nName=%s\nExec="%s"\nCategories=AudioVideo;Audio;\n' "$n" "$HOME/.local/bin/$n" > ~/.local/share/applications/"${n// /-}".desktop
done
echo "Installed EX-GF (VST3, LV2 and apps in ~/.local/bin)."
echo "Optional: download EX-SD-Card.zip from the release page and unzip it into ~/Music (giving ~/Music/EX SD Card)."
