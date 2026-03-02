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

rm -rf build_tmp
mkdir -p build_tmp

# IMPORTANT:
# - LAME + SpeexDSP sources are C, so they must be compiled with gcc (not g++).
# - Our app code is C++, compiled with g++, and we link everything with g++.

# We keep HAVE_CONFIG_H enabled for the vendored LAME build (it relies on types/headers).
# We patch LAME's config.h to disable x86/SSE paths on arm64 (see src/third_party/lame/config.h).
CFLAGS="-O2 -DHAVE_CONFIG_H \
  -I./src -I./src/third_party -I./src/third_party/speexdsp \
  -I./src/third_party/lame -I./src/third_party/lame/include \
  -I./src/third_party/lame/libmp3lame -I./src/third_party/lame/libmp3lame/vector \
  -I./src/third_party/lame/mpglib"

echo "Compiling C deps (SpeexDSP + LAME)..."

# LAME includes some optional x86/SSE-optimized sources. Those do not compile on arm64.
EXTRA_LAME_VEC=""
if [[ "$(uname -m)" == "x86_64" ]]; then
  EXTRA_LAME_VEC="src/third_party/lame/libmp3lame/vector/xmm_quantize_sub.c"
fi

gcc $CFLAGS -c \
  src/third_party/speexdsp/crowdnoise_speex_resampler.c \
  src/third_party/lame/libmp3lame/*.c \
  $EXTRA_LAME_VEC \
  src/third_party/lame/mpglib/common.c \
  src/third_party/lame/mpglib/interface.c \
  src/third_party/lame/mpglib/layer1.c \
  src/third_party/lame/mpglib/layer2.c \
  src/third_party/lame/mpglib/layer3.c \
  src/third_party/lame/mpglib/tabinit.c \
  src/third_party/lame/mpglib/decode_i386.c \
  src/third_party/lame/mpglib/dct64_i386.c

mv ./*.o build_tmp/

echo "Compiling C++ sources..."
g++ -std=c++17 -O2 -Wall -Wextra -I./src -c \
  src/mix_json_cli.cpp \
  src/crowdnoise_native_core.cpp \
  src/step1_decode_mp3_to_pcm.cpp

mv ./*.o build_tmp/

echo "Linking mix_json_cli..."
g++ -std=c++17 -O2 build_tmp/*.o -lm -o ./mix_json_cli

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

