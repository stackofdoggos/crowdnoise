#pragma once

#include <cstdint>
#include <string>

namespace crowdnoise {

struct CombineAudioRequest {
  // Path to mutable state JSON (Song_State).
  std::string songStatePath;

  // Output MP3 path to write.
  std::string outMp3Path;

  // 0..9 where 0 is best quality (largest), 9 is smallest.
  // This maps directly to LAME VBR quality in Step 7.
  int vbrQuality = 4;
};

struct CombineAudioResult {
  // Step 6 outputs (anti-clipping constant scale).
  float maxAbs = 0.0f;
  float scaleApplied = 1.0f;

  // The timeline length used for the mix (48kHz frames).
  uint64_t songFrames = 0;
};

// combineAudio() reads Song_State JSON and mixes all instruments' active_path
// into one MP3 file using the portable native pipeline (Steps 1-7).
//
// It does NOT modify Song_State.
CombineAudioResult combineAudio(const CombineAudioRequest& req);

}  // namespace crowdnoise

