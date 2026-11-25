#!/bin/bash
# mcoc-add-windows.sh <game folder> [cache folder]
#
# Makes a Marvel Contest of Champions dump (the Linux one: mcoc2.x86_64,
# UnityPlayer.so, mcoc2_Data) run on Windows too, from the same folder (see
# src/loader/rawthrills/mcoc/win/mcocWin.c):
#
#   mcoc2.exe, mcocwin.dll        ours (build-win64)
#   UnityPlayer.dll,
#   MonoBleedingEdge\             Unity 2021.3.23f1's Windows player (Mono,
#                                 x86-64), from Unity's download server
#   mcoc2_Data\Managed_win32\     the Windows builds of Mono's class libraries
#                                 the game ships (Linux's are in Managed), from
#                                 the Linux editor's Mono
#
# Nothing of the Linux player's is changed. Unity's files are fetched from
# download.unity3d.com (about 3 GB the first time: the Windows player support
# package and the editor, streamed; only what is needed is kept in the cache).
# Needs curl, 7z, bsdtar, xz, md5sum.
set -euo pipefail

GAME=${1:?usage: mcoc-add-windows.sh <game folder> [cache folder]}
CACHE=${2:-${XDG_CACHE_HOME:-$HOME/.cache}/linuxloader-mcoc-unity}
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD:-$HERE/build-win64}
UNITY=https://download.unity3d.com/download_unity/213b516bf396
PKG=MacEditorTargetInstaller/UnitySetup-Windows-Mono-Support-for-Editor-2021.3.23f1.pkg
PKG_MD5=08e03ae4bab0d55ce29f00c038f62063
EDITOR=LinuxEditorInstaller/Unity.tar.xz

[ -d "$GAME/mcoc2_Data/Managed" ] || { echo "$GAME: no mcoc2_Data/Managed" >&2; exit 1; }
for f in mcoc2.exe mcocwin.dll; do
    [ -f "$BUILD/$f" ] || { echo "$BUILD/$f missing: build the win64 tree first" >&2; exit 1; }
done
mkdir -p "$CACHE"

# Unity's Windows player (the release build, 64-bit Mono).
PLAYER=$CACHE/win64_player_nondevelopment_mono
if [ ! -f "$PLAYER/UnityPlayer.dll" ]; then
    work=$(mktemp -d "$CACHE/pkg.XXXXXX")
    echo "fetching Unity's Windows player support (566 MB)..."
    curl -fL -o "$work/win.pkg" "$UNITY/$PKG"
    echo "$PKG_MD5  $work/win.pkg" | md5sum -c --quiet
    (cd "$work" && 7z x -y win.pkg >/dev/null && mkdir pl &&
        gzip -dc TargetSupport.pkg.tmp/Payload | bsdtar -x -C pl \
            'Variations/win64_player_nondevelopment_mono/UnityPlayer.dll' \
            'Variations/win64_player_nondevelopment_mono/MonoBleedingEdge')
    rm -rf "$PLAYER"
    mv "$work/pl/Variations/win64_player_nondevelopment_mono" "$PLAYER"
    rm -rf "$work"
fi

# Mono's class libraries, Linux's and Windows' builds (to tell the game's
# from its own assemblies).
MONO=$CACHE/mono
if [ ! -f "$MONO/unityjit-win32/mscorlib.dll" ]; then
    echo "fetching Unity's Linux editor, for its Mono (2.5 GB, streamed)..."
    rm -rf "$MONO.tmp" && mkdir -p "$MONO.tmp"
    curl -fsL "$UNITY/$EDITOR" | xz -dc | bsdtar -x -C "$MONO.tmp" \
        'Editor/Data/MonoBleedingEdge/lib/mono/unityjit-win32' 'Editor/Data/MonoBleedingEdge/lib/mono/unityjit-linux'
    rm -rf "$MONO"
    mv "$MONO.tmp/Editor/Data/MonoBleedingEdge/lib/mono" "$MONO"
    rm -rf "$MONO.tmp"
fi

# The game's copies of Linux's libraries, and their Windows builds.
WIN=$GAME/mcoc2_Data/Managed_win32
mkdir -p "$WIN"
n=0
for f in "$GAME"/mcoc2_Data/Managed/*.dll; do
    b=$(basename "$f")
    for sub in "" Facades/; do
        lin=$MONO/unityjit-linux/$sub$b win=$MONO/unityjit-win32/$sub$b
        if [ -f "$lin" ] && [ -f "$win" ] && cmp -s "$f" "$lin" && ! cmp -s "$lin" "$win"; then
            cp "$win" "$WIN/$b"
            n=$((n + 1))
            break
        fi
    done
done
echo "Managed_win32: $n of Mono's libraries in their Windows build"

cp "$PLAYER/UnityPlayer.dll" "$GAME/"
mkdir -p "$GAME/MonoBleedingEdge"
cp -r "$PLAYER/MonoBleedingEdge/." "$GAME/MonoBleedingEdge/"
cp "$BUILD/mcoc2.exe" "$BUILD/mcocwin.dll" "$GAME/"
echo "done: $GAME runs on Windows with mcoc2.exe (and still on Linux as before)"
