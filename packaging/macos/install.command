#!/bin/bash
# EX Suite installer for macOS: copies the plug-ins and apps into your user folders.
cd "$(dirname "$0")"
set -e
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components ~/Applications
for f in VST3/*.vst3; do rm -rf ~/Library/Audio/Plug-Ins/VST3/"$(basename "$f")"; ditto "$f" ~/Library/Audio/Plug-Ins/VST3/"$(basename "$f")"; done
for f in Components/*.component; do rm -rf ~/Library/Audio/Plug-Ins/Components/"$(basename "$f")"; ditto "$f" ~/Library/Audio/Plug-Ins/Components/"$(basename "$f")"; done
for f in Applications/*.app; do rm -rf ~/Applications/"$(basename "$f")"; ditto "$f" ~/Applications/"$(basename "$f")"; done
# the builds are not notarised: clear the download quarantine so macOS lets them load
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/EX\ *.vst3 ~/Library/Audio/Plug-Ins/Components/EX\ *.component ~/Applications/EX\ *.app 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Installed EX Suite. Apps are in ~/Applications; rescan plug-ins in your DAW."
echo "Optional: download EX-SD-Card.zip from the release page and unzip it into ~/Music (giving ~/Music/EX SD Card)."
