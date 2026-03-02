// Tiny CLI wrapper around crowdnoise::combineAudio (portable native pipeline).
//
// Usage:
//   combine_audio_cli SONG_STATE.json OUT.mp3 [--vbr 0..9]
//
// Example:
//   ./combine_audio_cli "testing data/Song_State.json" out.mp3 --vbr 4

#include "combine_audio.h"

#include <iostream>
#include <optional>
#include <string>

static std::optional<std::string> getArg(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) return std::string(argv[i + 1]);
  }
  return std::nullopt;
}

static void usage(const char* exe) {
  std::cerr << "Usage:\n"
            << "  " << exe << " SONG_STATE.json OUT.mp3 [--vbr 0..9]\n";
}

int main(int argc, char** argv) {
  try {
    if (argc < 3) {
      usage(argv[0]);
      return 2;
    }

    crowdnoise::CombineAudioRequest req;
    req.songStatePath = argv[1];
    req.outMp3Path = argv[2];

    if (const auto vbr = getArg(argc, argv, "--vbr")) {
      req.vbrQuality = std::stoi(*vbr);
    }

    const auto res = crowdnoise::combineAudio(req);
    std::cout << "OK\n"
              << "  wrote:        " << req.outMp3Path << "\n"
              << "  song_frames:  " << res.songFrames << " (48kHz)\n"
              << "  step6_maxAbs: " << res.maxAbs << "\n"
              << "  step6_scale:  " << res.scaleApplied << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }
}

