// Command-line proof-of-concept for the "mix multiple instrument MP3s" pipeline.
//
// Runs Steps 1-7:
// - Decode+standardize each MP3 to 48kHz stereo float (Step 1-2)
// - Convert startSeconds -> startFrame (Step 3)
// - Allocate silent mix buffer sized to latest end (Step 4)
// - Add each track into mix (Step 5)
// - Prevent clipping by scaling if needed (Step 6)
// - Encode to MP3 file using LAME (Step 7)
//
// Usage:
//   mix_cli OUT.mp3 IN1.mp3 START1_SEC [IN2.mp3 START2_SEC ...]
//
// Example:
//   mix_cli out.mp3 drums.mp3 0 piano.mp3 2.0

#include "crowdnoise_native_core.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static void PrintUsage(const char* exe) {
  std::cerr
      << "Usage:\n"
      << "  " << exe << " OUT.mp3 IN1.mp3 START1_SEC [IN2.mp3 START2_SEC ...]\n"
      << "\n"
      << "Example:\n"
      << "  " << exe << " out.mp3 drums.mp3 0 piano.mp3 2.0\n";
}

static bool ParseDouble(const char* s, double* out) {
  if (!s || !out) return false;
  char* endp = nullptr;
  const double v = std::strtod(s, &endp);
  if (!endp || *endp != '\0') return false;
  *out = v;
  return true;
}

int main(int argc, char** argv) {
  if (argc < 4 || ((argc - 2) % 2) != 0) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string outMp3Path = argv[1];
  const int trackCount = (argc - 2) / 2;

  std::vector<crowdnoise_pcm_f32_t> tracks(static_cast<size_t>(trackCount));
  std::vector<crowdnoise_track_placement_t> placements(static_cast<size_t>(trackCount));

  char err[512];

  // Step 1-3 for each track.
  for (int i = 0; i < trackCount; ++i) {
    const char* mp3Path = argv[2 + i * 2 + 0];
    const char* startSecStr = argv[2 + i * 2 + 1];

    double startSeconds = 0.0;
    if (!ParseDouble(startSecStr, &startSeconds)) {
      std::cerr << "Bad startSeconds: " << startSecStr << "\n";
      return 2;
    }

    std::memset(&tracks[static_cast<size_t>(i)], 0, sizeof(crowdnoise_pcm_f32_t));
    std::memset(err, 0, sizeof(err));

    const crowdnoise_result_t rc = crowdnoise_decode_standardize_mp3_file(
        mp3Path, &tracks[static_cast<size_t>(i)], err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Decode+standardize failed for " << mp3Path << ": " << err << "\n";
      // Cleanup any prior successes.
      for (int j = 0; j < i; ++j) {
        crowdnoise_free_pcm_f32(&tracks[static_cast<size_t>(j)]);
      }
      return 1;
    }

    const uint64_t startFrame = crowdnoise_seconds_to_sample_index(startSeconds, 48000);
    placements[static_cast<size_t>(i)].startFrame = startFrame;
    placements[static_cast<size_t>(i)].frameCount = tracks[static_cast<size_t>(i)].frameCount;
  }

  // Step 4: allocate silent mix buffer.
  crowdnoise_pcm_f32_t mix{};
  std::memset(err, 0, sizeof(err));
  {
    const crowdnoise_result_t rc = crowdnoise_step4_allocate_silent_mix_buffer(
        placements.data(), placements.size(), &mix, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 4 allocate failed: " << err << "\n";
      for (int i = 0; i < trackCount; ++i) crowdnoise_free_pcm_f32(&tracks[static_cast<size_t>(i)]);
      return 1;
    }
  }

  // Step 5: add each track into the mix.
  for (int i = 0; i < trackCount; ++i) {
    std::memset(err, 0, sizeof(err));
    const crowdnoise_result_t rc = crowdnoise_step5_add_track_to_mix(
        &tracks[static_cast<size_t>(i)],
        placements[static_cast<size_t>(i)].startFrame,
        &mix,
        err,
        sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 5 add failed (track " << i << "): " << err << "\n";
      for (int j = 0; j < trackCount; ++j) crowdnoise_free_pcm_f32(&tracks[static_cast<size_t>(j)]);
      crowdnoise_free_pcm_f32(&mix);
      return 1;
    }
  }

  // Step 6: prevent clipping (scale down only if needed).
  float maxAbs = 0.0f;
  float scaleApplied = 1.0f;
  std::memset(err, 0, sizeof(err));
  {
    const crowdnoise_result_t rc = crowdnoise_step6_prevent_clipping_in_place(
        &mix, &maxAbs, &scaleApplied, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 6 failed: " << err << "\n";
      for (int i = 0; i < trackCount; ++i) crowdnoise_free_pcm_f32(&tracks[static_cast<size_t>(i)]);
      crowdnoise_free_pcm_f32(&mix);
      return 1;
    }
  }

  // Step 7: encode to MP3 file.
  std::memset(err, 0, sizeof(err));
  {
    const int vbrQuality = 4;  // good default
    const crowdnoise_result_t rc = crowdnoise_step7_encode_mix_to_mp3_file(
        &mix, outMp3Path.c_str(), vbrQuality, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 7 encode failed: " << err << "\n";
      for (int i = 0; i < trackCount; ++i) crowdnoise_free_pcm_f32(&tracks[static_cast<size_t>(i)]);
      crowdnoise_free_pcm_f32(&mix);
      return 1;
    }
  }

  // Cleanup.
  for (int i = 0; i < trackCount; ++i) crowdnoise_free_pcm_f32(&tracks[static_cast<size_t>(i)]);
  crowdnoise_free_pcm_f32(&mix);

  std::cout << "Wrote: " << outMp3Path << "\n";
  std::cout << "Step 6 maxAbs=" << maxAbs << " scaleApplied=" << scaleApplied << "\n";
  return 0;
}

