# Simple Makefile build for CrowdNoise native CLIs.
#
# Targets:
#   make                # build bin/mix_json_cli + bin/repeat_sample_at_times_cli
#   make clean           # remove build artifacts
#   make run-mix-json    # build and run a known-good test
#   make run-repeat-hats # build and run a hats-times repeat test
#
# Notes:
# - This repo vendors LAME + a minimal SpeexDSP resampler under src/third_party/.
# - On Apple Silicon (arm64), we must NOT compile LAME's x86/SSE vector file.

UNAME_M := $(shell uname -m)

CC  ?= cc
CXX ?= c++

SRC_DIR   := src
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
BIN_DIR   := bin

# Common include paths (both C and C++).
INCLUDES := -I./$(SRC_DIR) \
            -I./$(SRC_DIR)/third_party \
            -I./$(SRC_DIR)/third_party/speexdsp \
            -I./$(SRC_DIR)/third_party/lame \
            -I./$(SRC_DIR)/third_party/lame/include \
            -I./$(SRC_DIR)/third_party/lame/libmp3lame \
            -I./$(SRC_DIR)/third_party/lame/libmp3lame/vector \
            -I./$(SRC_DIR)/third_party/lame/mpglib

CPPFLAGS := $(INCLUDES)

# LAME expects HAVE_CONFIG_H so it can include <config.h> from its vendored tree.
CFLAGS   := -O2 -DHAVE_CONFIG_H
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
LDLIBS   := -lm

# -----------------------
# Vendored C deps (LAME + SpeexDSP-resampler build unit)
# -----------------------

SPEEX_C_SRCS := $(SRC_DIR)/third_party/speexdsp/crowdnoise_speex_resampler.c

LAME_LIBMP3LAME_C_SRCS := $(wildcard $(SRC_DIR)/third_party/lame/libmp3lame/*.c)
LAME_MPGLIB_C_SRCS := \
  $(SRC_DIR)/third_party/lame/mpglib/common.c \
  $(SRC_DIR)/third_party/lame/mpglib/interface.c \
  $(SRC_DIR)/third_party/lame/mpglib/layer1.c \
  $(SRC_DIR)/third_party/lame/mpglib/layer2.c \
  $(SRC_DIR)/third_party/lame/mpglib/layer3.c \
  $(SRC_DIR)/third_party/lame/mpglib/tabinit.c \
  $(SRC_DIR)/third_party/lame/mpglib/decode_i386.c \
  $(SRC_DIR)/third_party/lame/mpglib/dct64_i386.c

LAME_VEC_C_SRCS :=
ifeq ($(UNAME_M),x86_64)
  LAME_VEC_C_SRCS += $(SRC_DIR)/third_party/lame/libmp3lame/vector/xmm_quantize_sub.c
endif

VENDOR_C_SRCS := $(SPEEX_C_SRCS) $(LAME_LIBMP3LAME_C_SRCS) $(LAME_VEC_C_SRCS) $(LAME_MPGLIB_C_SRCS)
VENDOR_C_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(VENDOR_C_SRCS))

# -----------------------
# C++ app sources
# -----------------------

CORE_CPP_SRCS := \
  $(SRC_DIR)/crowdnoise_native_core.cpp \
  $(SRC_DIR)/step1_decode_mp3_to_pcm.cpp

CORE_CPP_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(CORE_CPP_SRCS))

MIX_JSON_CPP_SRCS := \
  $(SRC_DIR)/mix_json_cli.cpp

MIX_JSON_CPP_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(MIX_JSON_CPP_SRCS))

REPEAT_TIMES_CPP_SRCS := \
  $(SRC_DIR)/repeat_sample_at_times_cli.cpp

REPEAT_TIMES_CPP_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(REPEAT_TIMES_CPP_SRCS))

APP_CPP_OBJS := $(CORE_CPP_OBJS) $(MIX_JSON_CPP_OBJS) $(REPEAT_TIMES_CPP_OBJS)

# -----------------------
# Top-level targets
# -----------------------

.PHONY: all clean run-mix-json run-repeat run-repeat-hats
all: $(BIN_DIR)/mix_json_cli $(BIN_DIR)/repeat_sample_at_times_cli

$(BIN_DIR)/mix_json_cli: $(VENDOR_C_OBJS) $(CORE_CPP_OBJS) $(MIX_JSON_CPP_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BIN_DIR)/repeat_sample_at_times_cli: $(VENDOR_C_OBJS) $(CORE_CPP_OBJS) $(REPEAT_TIMES_CPP_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

# C compile rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# C++ compile rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

run-mix-json: $(BIN_DIR)/mix_json_cli
	./$(BIN_DIR)/mix_json_cli "testing data/Song_State.json" out.mp3

# Generic repeat runner (override vars on command line):
#   make run-repeat TIMES="path/to/times.csv" SAMPLE="path/to/hit.mp3" OUT="my.mp3"
TIMES ?= output/trackDecomp/drumTrack/drum/hats_times.csv
SAMPLE ?= testing\ data/remade_drums.mp3
OUT ?= out_repeat.mp3

run-repeat: $(BIN_DIR)/repeat_sample_at_times_cli
	./$(BIN_DIR)/repeat_sample_at_times_cli --times "$(TIMES)" --sample "$(SAMPLE)" --out "$(OUT)"

run-repeat-hats: $(BIN_DIR)/repeat_sample_at_times_cli
	./$(BIN_DIR)/repeat_sample_at_times_cli \
	  --times "output/trackDecomp/drumTrack/drum/hats_times.csv" \
	  --sample "testing data/remade_drums.mp3" \
	  --out out_hats.mp3

