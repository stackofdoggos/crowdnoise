// Step 1 from techspec.md:
// Decode each MP3 into raw sound numbers (PCM samples).
//
// Mobile compatibility:
// - Android APK assets are not filesystem paths; prefer DecodeMp3BytesToPcmF32() for assets.
// - Decode is streaming/chunked to avoid large temporary allocations.
// - No exceptions are required; functions return bool and optional error string.
//
// MP3 decoding is not part of standard C++, so we use a tiny C/C++ decoder library:
// dr_mp3 (single-header) from dr_libs.
//
// How to wire in dr_mp3:
// 1) Download dr_mp3.h from:
//    https://github.com/mackron/dr_libs/blob/master/dr_mp3.h
// 2) Place it somewhere your build can include, e.g.:
//    - src/third_party/dr_mp3.h
// 3) Update the include below accordingly.
//
// IMPORTANT:
// - DR_MP3_IMPLEMENTATION must be defined in exactly ONE .cpp file in your whole project.

#include "step1_decode_mp3_to_pcm.h"

#include <algorithm>
#include <cmath>
#include <limits>

// dr_mp3 header vendored into src/third_party/dr_mp3.h
#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"

// Step 2 resampling:
// We use a small built-in linear resampler to avoid external dependencies.
// This is good enough for offline mixing; it can be upgraded later if needed.

namespace {
bool SetError(std::string* errorMessage, const char* msg) {
  if (errorMessage) *errorMessage = msg ? msg : "Unknown error";
  return false;
}

bool ReserveIfPossible(drmp3* mp3, DecodedTrack* out) {
  // drmp3_get_pcm_frame_count can return 0 if unknown.
  const drmp3_uint64 framesHint = drmp3_get_pcm_frame_count(mp3);
  if (framesHint == 0) return true;

  // Convert to samples = frames * channels with overflow checks.
  const uint64_t channels = out->channels;
  if (channels == 0) return true;

  const unsigned long long maxSizeT = static_cast<unsigned long long>(std::numeric_limits<size_t>::max());
  const unsigned long long samplesULL = static_cast<unsigned long long>(framesHint) * static_cast<unsigned long long>(channels);
  if (samplesULL > maxSizeT) return true;

  out->pcm.reserve(static_cast<size_t>(samplesULL));
  return true;
}

bool DecodeFromDrMp3(drmp3* mp3, DecodedTrack* out, std::string* errorMessage) {
  if (!mp3 || !out) return SetError(errorMessage, "Invalid arguments");

  // dr_mp3 exposes these as top-level fields on the decoder.
  out->sampleRate = mp3->sampleRate;
  out->channels = mp3->channels;
  out->frameCount = 0;
  out->pcm.clear();

  if (out->sampleRate == 0 || out->channels == 0) {
    return SetError(errorMessage, "Invalid MP3 config (sampleRate/channels)");
  }

  ReserveIfPossible(mp3, out);

  // Decode in chunks.
  constexpr drmp3_uint64 kChunkFrames = 4096;
  std::vector<float> chunk(static_cast<size_t>(kChunkFrames * out->channels));

  drmp3_uint64 totalFrames = 0;
  for (;;) {
    const drmp3_uint64 framesRead = drmp3_read_pcm_frames_f32(mp3, kChunkFrames, chunk.data());
    if (framesRead == 0) break;

    const size_t samplesRead = static_cast<size_t>(framesRead * out->channels);
    out->pcm.insert(out->pcm.end(), chunk.begin(), chunk.begin() + samplesRead);
    totalFrames += framesRead;
  }

  out->frameCount = static_cast<uint64_t>(totalFrames);
  return true;
}
}  // namespace

bool DecodeMp3BytesToPcmF32(const uint8_t* mp3Data,
                           size_t mp3SizeBytes,
                           DecodedTrack* out,
                           std::string* errorMessage) {
  if (!mp3Data || mp3SizeBytes == 0 || !out) {
    return SetError(errorMessage, "Invalid arguments");
  }

  drmp3 mp3{};
  if (!drmp3_init_memory(&mp3, mp3Data, mp3SizeBytes, nullptr)) {
    return SetError(errorMessage, "Failed to init MP3 decoder from memory");
  }

  const bool ok = DecodeFromDrMp3(&mp3, out, errorMessage);
  drmp3_uninit(&mp3);
  return ok;
}

bool DecodeMp3FileToPcmF32(const std::string& filePath,
                           DecodedTrack* out,
                           std::string* errorMessage) {
  if (filePath.empty() || !out) {
    return SetError(errorMessage, "Invalid arguments");
  }

  drmp3 mp3{};
  if (!drmp3_init_file(&mp3, filePath.c_str(), nullptr)) {
    return SetError(errorMessage, "Failed to init MP3 decoder from file path");
  }

  const bool ok = DecodeFromDrMp3(&mp3, out, errorMessage);
  drmp3_uninit(&mp3);
  return ok;
}

namespace {
bool UpmixMonoToStereo(const DecodedTrack& inMono, DecodedTrack* outStereo, std::string* errorMessage) {
  if (!outStereo) return SetError(errorMessage, "Invalid arguments");
  if (inMono.channels != 1) return SetError(errorMessage, "UpmixMonoToStereo expects mono input");

  outStereo->sampleRate = inMono.sampleRate;
  outStereo->channels = 2;
  outStereo->frameCount = inMono.frameCount;
  outStereo->pcm.resize(static_cast<size_t>(inMono.frameCount * 2));

  const float* src = inMono.pcm.data();
  float* dst = outStereo->pcm.data();
  for (uint64_t i = 0; i < inMono.frameCount; ++i) {
    const float s = src[i];
    dst[2 * i + 0] = s;
    dst[2 * i + 1] = s;
  }

  return true;
}

bool ResampleInterleavedF32(const float* inInterleaved,
                            uint64_t inFrames,
                            uint32_t channels,
                            uint32_t inRate,
                            uint32_t outRate,
                            std::vector<float>* outInterleaved,
                            uint64_t* outFrames,
                            std::string* errorMessage) {
  if (!inInterleaved || !outInterleaved || !outFrames) return SetError(errorMessage, "Invalid arguments");
  if (channels == 0 || (channels != 1 && channels != 2)) return SetError(errorMessage, "Unsupported channel count");
  if (inRate == 0 || outRate == 0) return SetError(errorMessage, "Invalid sample rate");

  if (inFrames == 0) {
    outInterleaved->clear();
    *outFrames = 0;
    return true;
  }

  const double outFramesD = (static_cast<double>(inFrames) * static_cast<double>(outRate)) / static_cast<double>(inRate);
  uint64_t nOut = static_cast<uint64_t>(std::llround(outFramesD));
  if (nOut == 0) nOut = 1;

  // output index -> input position mapping:
  //   inPos = outIndex * (inRate / outRate)
  const double step = static_cast<double>(inRate) / static_cast<double>(outRate);

  outInterleaved->assign(static_cast<size_t>(nOut) * channels, 0.0f);

  for (uint64_t outF = 0; outF < nOut; ++outF) {
    const double inPos = static_cast<double>(outF) * step;
    const double inPosClamped = std::min<double>(inPos, static_cast<double>(inFrames - 1));
    const uint64_t i0 = static_cast<uint64_t>(std::floor(inPosClamped));
    const uint64_t i1 = (i0 + 1 < inFrames) ? (i0 + 1) : i0;
    const float frac = static_cast<float>(inPosClamped - static_cast<double>(i0));

    const size_t base0 = static_cast<size_t>(i0) * channels;
    const size_t base1 = static_cast<size_t>(i1) * channels;
    const size_t dstBase = static_cast<size_t>(outF) * channels;

    for (uint32_t ch = 0; ch < channels; ++ch) {
      const float a = inInterleaved[base0 + ch];
      const float b = inInterleaved[base1 + ch];
      (*outInterleaved)[dstBase + ch] = a + (b - a) * frac;
    }
  }

  *outFrames = nOut;
  return true;
}
}  // namespace

bool StandardizeTo48000HzStereoF32(const DecodedTrack& in,
                                  DecodedTrack* out,
                                  std::string* errorMessage) {
  if (!out) return SetError(errorMessage, "Invalid arguments");
  if (in.sampleRate == 0 || in.channels == 0) return SetError(errorMessage, "Invalid input track metadata");
  if (in.frameCount == 0) {
    out->sampleRate = 48000;
    out->channels = 2;
    out->frameCount = 0;
    out->pcm.clear();
    return true;
  }
  const uint64_t expectedSamples = in.frameCount * in.channels;
  if (in.pcm.size() < expectedSamples) return SetError(errorMessage, "Input PCM buffer too small");
  if (in.channels != 1 && in.channels != 2) return SetError(errorMessage, "Unsupported input channel count");

  // 1) Resample to 48kHz (keep channel count for now).
  DecodedTrack resampled;
  resampled.sampleRate = 48000;
  resampled.channels = in.channels;

  if (in.sampleRate == 48000) {
    resampled.frameCount = in.frameCount;
    resampled.pcm = in.pcm;
  } else {
    uint64_t outFrames = 0;
    if (!ResampleInterleavedF32(in.pcm.data(),
                                in.frameCount,
                                in.channels,
                                in.sampleRate,
                                /*outRate=*/48000,
                                &resampled.pcm,
                                &outFrames,
                                errorMessage)) {
      return false;
    }
    resampled.frameCount = outFrames;
  }

  // 2) Ensure stereo (2 channels).
  if (resampled.channels == 2) {
    *out = std::move(resampled);
    out->sampleRate = 48000;
    return true;
  }

  // Mono -> stereo upmix by duplication (no phase/coloration).
  DecodedTrack stereo;
  if (!UpmixMonoToStereo(resampled, &stereo, errorMessage)) return false;
  stereo.sampleRate = 48000;
  *out = std::move(stereo);
  return true;
}

/*
Example usage (file path):

  DecodedTrack track;
  std::string err;
  if (!DecodeMp3FileToPcmF32("/path/to/drums.mp3", &track, &err)) {
    // handle err
  }

Example usage (Android assets or any in-memory bytes):

  // Read MP3 bytes using platform APIs into mp3Bytes...
  DecodedTrack track;
  std::string err;
  if (!DecodeMp3BytesToPcmF32(mp3Bytes.data(), mp3Bytes.size(), &track, &err)) {
    // handle err
  }
*/

