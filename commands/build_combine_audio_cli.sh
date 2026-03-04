#!/usr/bin/env bash
set -euo pipefail

# Build the Song_State-driven combine-audio CLI for CrowdNoise.
#
# Usage:
#   ./build_combine_audio_cli.sh
#
# Then run:
#   ./combine_audio_cli "testing data/Song_State.json" out.mp3

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

echo "Building in: $PWD"

rm -rf build_tmp_combine
mkdir -p build_tmp_combine

# Dependencies:
# - MP3 decode: dr_mp3 (vendored header in src/third_party/dr_mp3.h)
# - MP3 encode: libmp3lame (system library; on macOS often installed via Homebrew)

INCLUDES="-I./src"
LIBS="-lmp3lame -lm"

# Homebrew defaults.
if [[ -d /opt/homebrew/include ]]; then
  INCLUDES="$INCLUDES -I/opt/homebrew/include"
fi
if [[ -d /opt/homebrew/lib ]]; then
  LIBS="-L/opt/homebrew/lib $LIBS"
fi

# Intel Homebrew / legacy.
if [[ -d /usr/local/include ]]; then
  INCLUDES="$INCLUDES -I/usr/local/include"
fi
if [[ -d /usr/local/lib ]]; then
  LIBS="-L/usr/local/lib $LIBS"
fi

echo "Compiling C++ sources..."
g++ -std=c++17 -O2 -Wall -Wextra $INCLUDES -c \
  src/combine_audio_cli.cpp \
  src/combine_audio.cpp \
  src/crowdnoise_native_core.cpp \
  src/step1_decode_mp3_to_pcm.cpp

mv ./*.o build_tmp_combine/

echo "Linking combine_audio_cli..."
g++ -std=c++17 -O2 build_tmp_combine/*.o $LIBS -o ./combine_audio_cli

# If the command was copy/pasted with CRLF, sometimes the output file ends up named "combine_audio_cli\r".
if [[ -e $'./combine_audio_cli\r' && ! -e ./combine_audio_cli ]]; then
  echo "Detected combine_audio_cli with CR in filename; fixing."
  mv $'./combine_audio_cli\r' ./combine_audio_cli
fi

chmod +x ./combine_audio_cli 2>/dev/null || true

echo
echo "Built:"
ls -lb ./combine_audio_cli*
echo
echo "Run:"
echo "  ./combine_audio_cli \"testing data/Song_State.json\" out.mp3"

