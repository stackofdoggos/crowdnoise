#!/usr/bin/env bash
set -euo pipefail

# Build the JSON-driven mixer CLI for CrowdNoise.
#
# Usage:
#   ./build_mix_json_cli.sh
#
# Then run:
#   ./mix_json_cli testFileFromPersonA.json out.mp3

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

echo "Building in: $PWD"

g++ -std=c++17 -O2 -Wall -Wextra -DHAVE_CONFIG_H \
  -I./src -I./src/third_party -I./src/third_party/speexdsp \
  -I./src/third_party/lame -I./src/third_party/lame/include \
  -I./src/third_party/lame/libmp3lame -I./src/third_party/lame/mpglib \
  src/mix_json_cli.cpp src/crowdnoise_native_core.cpp src/step1_decode_mp3_to_pcm.cpp \
  src/third_party/speexdsp/crowdnoise_speex_resampler.c \
  src/third_party/lame/libmp3lame/*.c \
  src/third_party/lame/mpglib/common.c src/third_party/lame/mpglib/interface.c \
  src/third_party/lame/mpglib/layer1.c src/third_party/lame/mpglib/layer2.c \
  src/third_party/lame/mpglib/layer3.c src/third_party/lame/mpglib/tabinit.c \
  -lm -o ./mix_json_cli

# If the command was copy/pasted with CRLF, sometimes the output file ends up named "mix_json_cli\r".
if [[ -e $'./mix_json_cli\r' && ! -e ./mix_json_cli ]]; then
  echo "Detected mix_json_cli with CR in filename; fixing."
  mv $'./mix_json_cli\r' ./mix_json_cli
fi

chmod +x ./mix_json_cli 2>/dev/null || true

echo
echo "Built:"
ls -lb ./mix_json_cli*
echo
echo "Run:"
echo "  ./mix_json_cli testFileFromPersonA.json out.mp3"

