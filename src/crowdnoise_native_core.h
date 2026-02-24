// C-compatible wrapper API for the CrowdNoise native core.
//
// Why this exists:
// - Swift/JNI don't interoperate cleanly with C++ types like std::vector/std::string.
// - This header exposes a tiny "C ABI" surface that both iOS and Android can call.
//
// What it provides right now:
// - Decode MP3 (from file path OR in-memory bytes) -> PCM float32 interleaved stereo @ 48kHz
// - Free functions for buffers allocated by the native core
// - Utility function for Step 3 math (seconds -> sample index)
//
// Threading:
// - Functions are stateless; safe to call concurrently as long as you manage buffers per call.

#pragma once

#include <stddef.h>   // size_t
#include <stdint.h>   // uint8_t, uint32_t, uint64_t

#ifdef __cplusplus
extern "C" {
#endif

typedef enum crowdnoise_result_t {
  CROWDNOISE_OK = 0,
  CROWDNOISE_ERROR_INVALID_ARGS = 1,
  CROWDNOISE_ERROR_DECODE_FAILED = 2,
  CROWDNOISE_ERROR_STANDARDIZE_FAILED = 3,
  CROWDNOISE_ERROR_OUT_OF_MEMORY = 4,
  CROWDNOISE_ERROR_INTERNAL = 100
} crowdnoise_result_t;

// PCM buffer returned to the caller.
// - pcmInterleaved: float samples in [-1, +1]
// - frameCount: number of frames (1 frame = 1 sample per channel)
// - sampleRate: will be 48000 when returned by decode+standardize functions
// - channels: will be 2 when returned by decode+standardize functions
typedef struct crowdnoise_pcm_f32_t {
  float* pcmInterleaved;
  uint64_t frameCount;
  uint32_t sampleRate;
  uint32_t channels;
} crowdnoise_pcm_f32_t;

// Decode MP3 from a filesystem path (UTF-8) and standardize to:
// - 48,000 Hz
// - stereo (2 channels)
// - float32 interleaved
//
// On error, returns a non-zero code and (optionally) writes a human-readable
// message into errorBuf.
crowdnoise_result_t crowdnoise_decode_standardize_mp3_file(const char* filePathUtf8,
                                                          crowdnoise_pcm_f32_t* outPcm,
                                                          char* errorBuf,
                                                          size_t errorBufSize);

// Decode MP3 from a memory buffer and standardize to 48k stereo float PCM.
//
// This is especially useful for Android APK assets (which are not filesystem paths).
crowdnoise_result_t crowdnoise_decode_standardize_mp3_bytes(const uint8_t* mp3Data,
                                                           size_t mp3SizeBytes,
                                                           crowdnoise_pcm_f32_t* outPcm,
                                                           char* errorBuf,
                                                           size_t errorBufSize);

// Free a PCM buffer allocated by this library.
// Safe to call on an already-freed/empty buffer.
void crowdnoise_free_pcm_f32(crowdnoise_pcm_f32_t* pcm);

// Step 3 helper:
// Convert seconds -> sample index for a given sampleRate.
// Uses rounding to the nearest sample.
uint64_t crowdnoise_seconds_to_sample_index(double startSeconds, uint32_t sampleRate);

// Step 4:
// Given a list of track placements (each with a startFrame and frameCount),
// allocate a zero-filled final mix buffer that is long enough to hold the track
// that ends the latest.
//
// Output format is always:
// - 48,000 Hz
// - stereo (2 channels)
// - float32 interleaved
//
// This does NOT mix audio yet; it only allocates the silent timeline buffer.
typedef struct crowdnoise_track_placement_t {
  uint64_t startFrame;   // where this track begins on the timeline (48kHz frames)
  uint64_t frameCount;   // how long this track is (in frames)
} crowdnoise_track_placement_t;

crowdnoise_result_t crowdnoise_step4_allocate_silent_mix_buffer(const crowdnoise_track_placement_t* placements,
                                                               size_t placementCount,
                                                               crowdnoise_pcm_f32_t* outMixBuffer,
                                                               char* errorBuf,
                                                               size_t errorBufSize);

// Step 5:
// Mix (add) a standardized track into an existing mix buffer.
//
// Requirements:
// - trackPcm must be 48,000 Hz, stereo (2 channels), float32 interleaved.
// - ioMixBuffer must be 48,000 Hz, stereo (2 channels), float32 interleaved.
// - startFrame is where the track begins on the timeline (in 48kHz frames).
//
// This performs "mixing is addition":
//   mix[(startFrame+i)*2 + ch] += track[i*2 + ch]
//
// This function does NOT do Step 6 (anti-clipping). After adding all tracks,
// run a Step 6 scaling pass.
crowdnoise_result_t crowdnoise_step5_add_track_to_mix(const crowdnoise_pcm_f32_t* trackPcm,
                                                     uint64_t startFrame,
                                                     crowdnoise_pcm_f32_t* ioMixBuffer,
                                                     char* errorBuf,
                                                     size_t errorBufSize);

// Step 6:
// Prevent clipping (distortion) by applying one constant "master volume" scale
// to the entire mix buffer if any sample exceeds [-1, +1].
//
// Algorithm:
// - maxAbs = max(abs(sample)) across the whole buffer
// - if maxAbs <= 1.0: do nothing
// - else: scale = 0.999 / maxAbs (small safety headroom), then multiply every
//         sample by scale.
//
// IMPORTANT:
// - This does NOT clamp samples; clamping would introduce distortion.
// - This keeps instrument timing and relative balance intact.
//
// Optional outputs:
// - outMaxAbs: the measured maxAbs before scaling (can be NULL)
// - outScaleApplied: the final scale factor applied (1.0 if no scaling) (can be NULL)
crowdnoise_result_t crowdnoise_step6_prevent_clipping_in_place(crowdnoise_pcm_f32_t* ioMixBuffer,
                                                              float* outMaxAbs,
                                                              float* outScaleApplied,
                                                              char* errorBuf,
                                                              size_t errorBufSize);

// Step 7:
// Encode the final 48kHz stereo float mix buffer to an MP3 file on disk.
//
// This uses the vendored LAME encoder (src/third_party/lame).
//
// Requirements:
// - mixBuffer must be 48,000 Hz stereo (2 channels) float32 interleaved.
// - It should already be made safe via Step 6 (no samples outside [-1, +1]).
//
// Encoding notes:
// - PCM is fed to LAME in chunks (streaming) to avoid large temporary buffers.
// - Output is written incrementally to `outFilePathUtf8`.
//
// Parameters:
// - vbrQuality: 0..9, where 0 is best quality (largest) and 9 is lowest quality (smallest).
//   A good default is 4 or 5.
crowdnoise_result_t crowdnoise_step7_encode_mix_to_mp3_file(const crowdnoise_pcm_f32_t* mixBuffer,
                                                           const char* outFilePathUtf8,
                                                           int vbrQuality,
                                                           char* errorBuf,
                                                           size_t errorBufSize);

#ifdef __cplusplus
}  // extern "C"
#endif

