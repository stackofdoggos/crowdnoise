// Implementation of the C-compatible wrapper API.

#include "crowdnoise_native_core.h"

#include "step1_decode_mp3_to_pcm.h"

#include <cstdlib>    // malloc, calloc, free
#include <cmath>      // llround
#include <cstring>    // memcpy, strncpy
#include <cstdio>     // FILE, fopen, fwrite, fclose
#include <limits>
#include <string>
#include <vector>

// MP3 encoder.
//
// In some environments we vendor LAME; on macOS dev machines it's often installed via Homebrew.
// Prefer system headers when available, but keep a vendored fallback.
#if defined(__has_include)
#  if __has_include(<lame/lame.h>)
#    include <lame/lame.h>
#  elif __has_include(<lame.h>)
#    include <lame.h>
#  else
#    include "third_party/lame/include/lame.h"
#  endif
#else
#  include "third_party/lame/include/lame.h"
#endif

namespace {
void WriteError(char* errorBuf, size_t errorBufSize, const std::string& msg) {
  if (!errorBuf || errorBufSize == 0) return;
  // strncpy doesn't guarantee null-termination if truncated.
  const size_t n = (msg.size() < (errorBufSize - 1)) ? msg.size() : (errorBufSize - 1);
  if (n > 0) {
    std::memcpy(errorBuf, msg.data(), n);
  }
  errorBuf[n] = '\0';
}

void ClearOut(crowdnoise_pcm_f32_t* outPcm) {
  if (!outPcm) return;
  outPcm->pcmInterleaved = nullptr;
  outPcm->frameCount = 0;
  outPcm->sampleRate = 0;
  outPcm->channels = 0;
}

crowdnoise_result_t CopyToCBuffer(const DecodedTrack& track, crowdnoise_pcm_f32_t* outPcm) {
  if (!outPcm) return CROWDNOISE_ERROR_INVALID_ARGS;
  ClearOut(outPcm);

  const uint64_t sampleCount = track.frameCount * track.channels;
  if (sampleCount == 0) {
    outPcm->frameCount = 0;
    outPcm->sampleRate = track.sampleRate;
    outPcm->channels = track.channels;
    return CROWDNOISE_OK;
  }

  // Allocate with malloc/free so the caller can safely free via our API.
  const size_t bytes = static_cast<size_t>(sampleCount) * sizeof(float);
  float* buf = static_cast<float*>(std::malloc(bytes));
  if (!buf) return CROWDNOISE_ERROR_OUT_OF_MEMORY;

  std::memcpy(buf, track.pcm.data(), bytes);

  outPcm->pcmInterleaved = buf;
  outPcm->frameCount = track.frameCount;
  outPcm->sampleRate = track.sampleRate;
  outPcm->channels = track.channels;
  return CROWDNOISE_OK;
}

crowdnoise_result_t AllocateZeroedPcm48kStereo(uint64_t frameCount, crowdnoise_pcm_f32_t* outPcm) {
  if (!outPcm) return CROWDNOISE_ERROR_INVALID_ARGS;
  ClearOut(outPcm);

  outPcm->sampleRate = 48000;
  outPcm->channels = 2;
  outPcm->frameCount = frameCount;

  if (frameCount == 0) {
    outPcm->pcmInterleaved = nullptr;
    return CROWDNOISE_OK;
  }

  // totalSamples = frames * channels (2).
  if (frameCount > (std::numeric_limits<uint64_t>::max() / 2u)) {
    return CROWDNOISE_ERROR_OUT_OF_MEMORY;
  }
  const uint64_t totalSamples64 = frameCount * 2u;
  if (totalSamples64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / sizeof(float))) {
    return CROWDNOISE_ERROR_OUT_OF_MEMORY;
  }
  const size_t bytes = static_cast<size_t>(totalSamples64) * sizeof(float);

  float* buf = static_cast<float*>(std::calloc(1, bytes));  // zero-filled (silence)
  if (!buf) return CROWDNOISE_ERROR_OUT_OF_MEMORY;

  outPcm->pcmInterleaved = buf;
  return CROWDNOISE_OK;
}

crowdnoise_result_t DecodeThenStandardize(const DecodedTrack& decoded,
                                         crowdnoise_pcm_f32_t* outPcm,
                                         char* errorBuf,
                                         size_t errorBufSize) {
  if (!outPcm) return CROWDNOISE_ERROR_INVALID_ARGS;

  std::string err;
  DecodedTrack standardized;
  if (!StandardizeTo48000HzStereoF32(decoded, &standardized, &err)) {
    WriteError(errorBuf, errorBufSize, err.empty() ? "Standardize failed" : err);
    return CROWDNOISE_ERROR_STANDARDIZE_FAILED;
  }

  return CopyToCBuffer(standardized, outPcm);
}
}  // namespace

crowdnoise_result_t crowdnoise_decode_standardize_mp3_file(const char* filePathUtf8,
                                                          crowdnoise_pcm_f32_t* outPcm,
                                                          char* errorBuf,
                                                          size_t errorBufSize) {
  ClearOut(outPcm);
  if (!filePathUtf8 || filePathUtf8[0] == '\0' || !outPcm) {
    WriteError(errorBuf, errorBufSize, "Invalid arguments");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  std::string err;
  DecodedTrack decoded;
  if (!DecodeMp3FileToPcmF32(std::string(filePathUtf8), &decoded, &err)) {
    WriteError(errorBuf, errorBufSize, err.empty() ? "Decode failed" : err);
    return CROWDNOISE_ERROR_DECODE_FAILED;
  }

  return DecodeThenStandardize(decoded, outPcm, errorBuf, errorBufSize);
}

crowdnoise_result_t crowdnoise_decode_standardize_mp3_bytes(const uint8_t* mp3Data,
                                                           size_t mp3SizeBytes,
                                                           crowdnoise_pcm_f32_t* outPcm,
                                                           char* errorBuf,
                                                           size_t errorBufSize) {
  ClearOut(outPcm);
  if (!mp3Data || mp3SizeBytes == 0 || !outPcm) {
    WriteError(errorBuf, errorBufSize, "Invalid arguments");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  std::string err;
  DecodedTrack decoded;
  if (!DecodeMp3BytesToPcmF32(mp3Data, mp3SizeBytes, &decoded, &err)) {
    WriteError(errorBuf, errorBufSize, err.empty() ? "Decode failed" : err);
    return CROWDNOISE_ERROR_DECODE_FAILED;
  }

  return DecodeThenStandardize(decoded, outPcm, errorBuf, errorBufSize);
}

void crowdnoise_free_pcm_f32(crowdnoise_pcm_f32_t* pcm) {
  if (!pcm) return;
  if (pcm->pcmInterleaved) {
    std::free(pcm->pcmInterleaved);
  }
  ClearOut(pcm);
}

uint64_t crowdnoise_seconds_to_sample_index(double startSeconds, uint32_t sampleRate) {
  if (sampleRate == 0) return 0;
  if (startSeconds <= 0.0) return 0;
  const double idx = startSeconds * static_cast<double>(sampleRate);
  const long long rounded = std::llround(idx);
  return (rounded <= 0) ? 0u : static_cast<uint64_t>(rounded);
}

crowdnoise_result_t crowdnoise_step4_allocate_silent_mix_buffer(const crowdnoise_track_placement_t* placements,
                                                               size_t placementCount,
                                                               crowdnoise_pcm_f32_t* outMixBuffer,
                                                               char* errorBuf,
                                                               size_t errorBufSize) {
  ClearOut(outMixBuffer);
  if (!outMixBuffer) {
    WriteError(errorBuf, errorBufSize, "Invalid arguments");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  if (placementCount > 0 && !placements) {
    WriteError(errorBuf, errorBufSize, "Invalid arguments (placements)");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  // Find the latest ending track: max(startFrame + frameCount).
  uint64_t finalFrameCount = 0;
  for (size_t i = 0; i < placementCount; ++i) {
    const uint64_t start = placements[i].startFrame;
    const uint64_t len = placements[i].frameCount;

    // Overflow check for endFrame = start + len.
    if (len > 0 && start > (std::numeric_limits<uint64_t>::max() - len)) {
      WriteError(errorBuf, errorBufSize, "Track end time overflow");
      return CROWDNOISE_ERROR_INVALID_ARGS;
    }
    const uint64_t end = start + len;
    if (end > finalFrameCount) finalFrameCount = end;
  }

  const crowdnoise_result_t rc = AllocateZeroedPcm48kStereo(finalFrameCount, outMixBuffer);
  if (rc != CROWDNOISE_OK) {
    WriteError(errorBuf, errorBufSize, "Failed to allocate mix buffer");
    return rc;
  }

  return CROWDNOISE_OK;
}

crowdnoise_result_t crowdnoise_step5_add_track_to_mix(const crowdnoise_pcm_f32_t* trackPcm,
                                                     uint64_t startFrame,
                                                     crowdnoise_pcm_f32_t* ioMixBuffer,
                                                     char* errorBuf,
                                                     size_t errorBufSize) {
  if (!trackPcm || !ioMixBuffer) {
    WriteError(errorBuf, errorBufSize, "Invalid arguments");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (!trackPcm->pcmInterleaved && trackPcm->frameCount > 0) {
    WriteError(errorBuf, errorBufSize, "Invalid track PCM buffer");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (!ioMixBuffer->pcmInterleaved && ioMixBuffer->frameCount > 0) {
    WriteError(errorBuf, errorBufSize, "Invalid mix buffer");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  // Enforce standardized formats (Step 2 + Step 4 outputs).
  if (trackPcm->sampleRate != 48000 || trackPcm->channels != 2) {
    WriteError(errorBuf, errorBufSize, "Track must be 48kHz stereo");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (ioMixBuffer->sampleRate != 48000 || ioMixBuffer->channels != 2) {
    WriteError(errorBuf, errorBufSize, "Mix buffer must be 48kHz stereo");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  // Bounds check: ensure the track fits inside the mix timeline.
  const uint64_t trackFrames = trackPcm->frameCount;
  const uint64_t mixFrames = ioMixBuffer->frameCount;
  if (trackFrames == 0) return CROWDNOISE_OK;

  if (startFrame > mixFrames) {
    WriteError(errorBuf, errorBufSize, "startFrame is beyond mix buffer");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (trackFrames > (std::numeric_limits<uint64_t>::max() - startFrame)) {
    WriteError(errorBuf, errorBufSize, "Track end time overflow");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  const uint64_t endFrame = startFrame + trackFrames;
  if (endFrame > mixFrames) {
    WriteError(errorBuf, errorBufSize, "Track does not fit in mix buffer");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  // Mixing is addition (stereo interleaved).
  float* mix = ioMixBuffer->pcmInterleaved;
  const float* track = trackPcm->pcmInterleaved;

  // Convert to sample indices (2 samples per frame).
  // We also ensure we don't overflow size_t indexing on very large buffers.
  const uint64_t mixSampleOffset64 = startFrame * 2u;
  const uint64_t trackSampleCount64 = trackFrames * 2u;
  if (mixSampleOffset64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
      trackSampleCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    WriteError(errorBuf, errorBufSize, "Buffer too large to index");
    return CROWDNOISE_ERROR_OUT_OF_MEMORY;
  }

  const size_t mixSampleOffset = static_cast<size_t>(mixSampleOffset64);
  const size_t trackSampleCount = static_cast<size_t>(trackSampleCount64);

  for (size_t i = 0; i < trackSampleCount; ++i) {
    mix[mixSampleOffset + i] += track[i];
  }

  return CROWDNOISE_OK;
}

crowdnoise_result_t crowdnoise_step6_prevent_clipping_in_place(crowdnoise_pcm_f32_t* ioMixBuffer,
                                                              float* outMaxAbs,
                                                              float* outScaleApplied,
                                                              char* errorBuf,
                                                              size_t errorBufSize) {
  if (outMaxAbs) *outMaxAbs = 0.0f;
  if (outScaleApplied) *outScaleApplied = 1.0f;

  if (!ioMixBuffer) {
    WriteError(errorBuf, errorBufSize, "Invalid arguments");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (ioMixBuffer->sampleRate != 48000 || ioMixBuffer->channels != 2) {
    WriteError(errorBuf, errorBufSize, "Mix buffer must be 48kHz stereo");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (ioMixBuffer->frameCount == 0) return CROWDNOISE_OK;
  if (!ioMixBuffer->pcmInterleaved) {
    WriteError(errorBuf, errorBufSize, "Invalid mix buffer");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }

  const uint64_t totalSamples64 = ioMixBuffer->frameCount * 2u;
  if (totalSamples64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    WriteError(errorBuf, errorBufSize, "Buffer too large to index");
    return CROWDNOISE_ERROR_OUT_OF_MEMORY;
  }
  const size_t totalSamples = static_cast<size_t>(totalSamples64);

  // 1) Measure loudest absolute sample.
  float maxAbs = 0.0f;
  const float* x = ioMixBuffer->pcmInterleaved;
  for (size_t i = 0; i < totalSamples; ++i) {
    const float v = x[i];
    if (!std::isfinite(v)) {
      WriteError(errorBuf, errorBufSize, "Non-finite sample encountered (NaN/Inf)");
      return CROWDNOISE_ERROR_INTERNAL;
    }
    const float a = std::fabs(v);
    if (a > maxAbs) maxAbs = a;
  }

  if (outMaxAbs) *outMaxAbs = maxAbs;

  // 2) If safe, do nothing.
  if (maxAbs <= 1.0f) {
    if (outScaleApplied) *outScaleApplied = 1.0f;
    return CROWDNOISE_OK;
  }

  // 3) Scale the entire buffer down with a small safety margin to avoid exact-boundary clipping.
  constexpr float kSafety = 0.999f;
  const float scale = kSafety / maxAbs;
  if (!(scale > 0.0f) || !std::isfinite(scale)) {
    WriteError(errorBuf, errorBufSize, "Invalid scale factor");
    return CROWDNOISE_ERROR_INTERNAL;
  }

  float* y = ioMixBuffer->pcmInterleaved;
  for (size_t i = 0; i < totalSamples; ++i) {
    y[i] *= scale;
  }

  if (outScaleApplied) *outScaleApplied = scale;
  return CROWDNOISE_OK;
}

crowdnoise_result_t crowdnoise_step7_encode_mix_to_mp3_file(const crowdnoise_pcm_f32_t* mixBuffer,
                                                           const char* outFilePathUtf8,
                                                           int vbrQuality,
                                                           char* errorBuf,
                                                           size_t errorBufSize) {
  if (!mixBuffer || !outFilePathUtf8 || outFilePathUtf8[0] == '\0') {
    WriteError(errorBuf, errorBufSize, "Invalid arguments");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (mixBuffer->sampleRate != 48000 || mixBuffer->channels != 2) {
    WriteError(errorBuf, errorBufSize, "Mix buffer must be 48kHz stereo");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (mixBuffer->frameCount > 0 && !mixBuffer->pcmInterleaved) {
    WriteError(errorBuf, errorBufSize, "Invalid mix buffer");
    return CROWDNOISE_ERROR_INVALID_ARGS;
  }
  if (vbrQuality < 0) vbrQuality = 0;
  if (vbrQuality > 9) vbrQuality = 9;

  FILE* fp = std::fopen(outFilePathUtf8, "wb");
  if (!fp) {
    WriteError(errorBuf, errorBufSize, "Failed to open output MP3 file for writing");
    return CROWDNOISE_ERROR_INTERNAL;
  }

  lame_t lame = lame_init();
  if (!lame) {
    std::fclose(fp);
    WriteError(errorBuf, errorBufSize, "Failed to initialize LAME encoder");
    return CROWDNOISE_ERROR_INTERNAL;
  }

  // Configure for our standardized pipeline: 48kHz stereo.
  lame_set_in_samplerate(lame, 48000);
  lame_set_num_channels(lame, 2);
  lame_set_mode(lame, STEREO);

  // Use VBR for good quality/size tradeoff.
  lame_set_VBR(lame, vbr_default);
  lame_set_VBR_quality(lame, vbrQuality);  // 0=best, 9=smallest

  // Optional: set ID3 tags off by default (keeps output minimal).
  id3tag_init(lame);
  id3tag_set_pad(lame, 0);

  if (lame_init_params(lame) < 0) {
    lame_close(lame);
    std::fclose(fp);
    WriteError(errorBuf, errorBufSize, "Failed to initialize LAME parameters");
    return CROWDNOISE_ERROR_INTERNAL;
  }

  // Encode in chunks.
  constexpr size_t kChunkFrames = 1152 * 16;  // multiple of MP3 frame size; good throughput
  const float* in = mixBuffer->pcmInterleaved;
  uint64_t framesRemaining = mixBuffer->frameCount;
  uint64_t frameOffset = 0;

  // LAME output buffer: documented safe size is ~1.25*num_samples + 7200 bytes.
  // Here num_samples = frames per channel for the chunk.
  std::vector<short> pcmS16Interleaved;
  pcmS16Interleaved.reserve(kChunkFrames * 2);
  std::vector<unsigned char> mp3Buf;
  mp3Buf.resize(static_cast<size_t>(1.25 * (kChunkFrames) + 7200));

  while (framesRemaining > 0) {
    const size_t framesThis = static_cast<size_t>(std::min<uint64_t>(framesRemaining, kChunkFrames));

    // Convert float [-1, +1] -> int16, interleaved.
    pcmS16Interleaved.clear();
    pcmS16Interleaved.resize(framesThis * 2);

    const float* src = in + static_cast<size_t>(frameOffset * 2u);
    for (size_t i = 0; i < framesThis * 2; ++i) {
      const float v = src[i];
      if (!std::isfinite(v)) {
        lame_close(lame);
        std::fclose(fp);
        WriteError(errorBuf, errorBufSize, "Non-finite sample encountered (NaN/Inf)");
        return CROWDNOISE_ERROR_INTERNAL;
      }
      // Step 6 should already keep us in-range. Clamp defensively to avoid overflow.
      float x = v;
      if (x > 1.0f) x = 1.0f;
      if (x < -1.0f) x = -1.0f;

      // Map [-1,1] to int16. Use 32767 for +1.0 and -32768 for -1.0.
      const int sample = static_cast<int>(std::lrintf(x * 32767.0f));
      const int s16 = (sample < -32768) ? -32768 : (sample > 32767 ? 32767 : sample);
      pcmS16Interleaved[i] = static_cast<short>(s16);
    }

    const int mp3Bytes = lame_encode_buffer_interleaved(
        lame,
        pcmS16Interleaved.data(),
        static_cast<int>(framesThis),
        mp3Buf.data(),
        static_cast<int>(mp3Buf.size()));

    if (mp3Bytes < 0) {
      lame_close(lame);
      std::fclose(fp);
      WriteError(errorBuf, errorBufSize, "LAME encoding failed");
      return CROWDNOISE_ERROR_INTERNAL;
    }

    if (mp3Bytes > 0) {
      const size_t written = std::fwrite(mp3Buf.data(), 1, static_cast<size_t>(mp3Bytes), fp);
      if (written != static_cast<size_t>(mp3Bytes)) {
        lame_close(lame);
        std::fclose(fp);
        WriteError(errorBuf, errorBufSize, "Failed to write MP3 data to file");
        return CROWDNOISE_ERROR_INTERNAL;
      }
    }

    frameOffset += framesThis;
    framesRemaining -= framesThis;
  }

  // Flush encoder.
  const int flushBytes = lame_encode_flush(lame, mp3Buf.data(), static_cast<int>(mp3Buf.size()));
  if (flushBytes < 0) {
    lame_close(lame);
    std::fclose(fp);
    WriteError(errorBuf, errorBufSize, "LAME flush failed");
    return CROWDNOISE_ERROR_INTERNAL;
  }
  if (flushBytes > 0) {
    const size_t written = std::fwrite(mp3Buf.data(), 1, static_cast<size_t>(flushBytes), fp);
    if (written != static_cast<size_t>(flushBytes)) {
      lame_close(lame);
      std::fclose(fp);
      WriteError(errorBuf, errorBufSize, "Failed to write MP3 flush data");
      return CROWDNOISE_ERROR_INTERNAL;
    }
  }

  lame_close(lame);
  std::fclose(fp);
  return CROWDNOISE_OK;
}

