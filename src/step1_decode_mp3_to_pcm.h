#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Step 1 from techspec.md:
// Decode each MP3 into raw sound numbers (PCM samples).
//
// Mobile compatibility notes:
// - Android APK "assets/" are not regular filesystem paths; for those, read the MP3 bytes
//   using platform APIs and call DecodeMp3BytesToPcmF32().
// - For files in app storage (Documents/cache/tmp, etc.), DecodeMp3FileToPcmF32() is fine.
// - Output samples are float PCM in [-1, +1], interleaved by channel.
//
// This header is C++-only (no platform-specific includes). It should compile on
// iOS (Xcode/Clang) and Android (NDK/Clang).

struct DecodedTrack {
  uint32_t sampleRate = 0;   // e.g. 44100
  uint32_t channels = 0;     // 1=mono, 2=stereo
  uint64_t frameCount = 0;   // number of PCM frames (1 frame = 1 sample per channel)
  std::vector<float> pcm;    // interleaved: [L0,R0,L1,R1,...] if channels==2
};

// Decodes MP3 data already loaded into memory.
// Returns true on success, false on failure (errorMessage is optional).
bool DecodeMp3BytesToPcmF32(const uint8_t* mp3Data,
                           size_t mp3SizeBytes,
                           DecodedTrack* out,
                           std::string* errorMessage);

// Decodes MP3 from a filesystem path.
// Returns true on success, false on failure (errorMessage is optional).
bool DecodeMp3FileToPcmF32(const std::string& filePath,
                           DecodedTrack* out,
                           std::string* errorMessage);

// Step 2 from techspec.md:
// Standardize a decoded track to a common format for mixing:
// - Sample rate: 48,000 Hz
// - Channels: stereo (2)
// - Samples: float32 interleaved
//
// This uses a high-quality resampler (SpeexDSP resampler, quality=10) to minimize
// audible differences vs. the original.
bool StandardizeTo48000HzStereoF32(const DecodedTrack& in,
                                  DecodedTrack* out,
                                  std::string* errorMessage);

