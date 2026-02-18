#include "replace_track.h"

#include <iostream>
#include <optional>
#include <string>

// Tiny CLI so you can test replaceTrack() without the full app.
//
// Example:
//   ./replace_track \
//     --song-details data/Song_Details.json \
//     --song-state data/Song_State.json \
//     --instrument drums \
//     --new-track uploads/u1/remade_drums.mp3 \
//     --expected-revision 0 \
//     --user-id user123

static std::optional<std::string> getArg(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) return std::string(argv[i + 1]);
  }
  return std::nullopt;
}

int main(int argc, char** argv) {
  try {
    const auto songDetails = getArg(argc, argv, "--song-details");
    const auto songState = getArg(argc, argv, "--song-state");
    const auto instrument = getArg(argc, argv, "--instrument");
    const auto newTrack = getArg(argc, argv, "--new-track");

    if (!songDetails || !songState || !instrument || !newTrack) {
      std::cerr << "Missing required args.\n"
                   "Required: --song-details <path> --song-state <path> --instrument <name> --new-track <path>\n"
                   "Optional: --expected-revision <int> --user-id <id>\n";
      return 2;
    }

    crowdnoise::ReplaceTrackRequest req;
    req.songDetailsPath = *songDetails;
    req.songStatePath = *songState;
    req.instrumentName = *instrument;
    req.newTrackPath = *newTrack;

    if (const auto expected = getArg(argc, argv, "--expected-revision")) {
      req.expectedRevision = std::stoi(*expected);
    }
    if (const auto userId = getArg(argc, argv, "--user-id")) {
      req.userId = *userId;
    }

    const auto res = crowdnoise::replaceTrack(req);
    std::cout << "OK\n"
              << "  old_active_path: " << res.oldActivePath << "\n"
              << "  new_active_path: " << res.newActivePath << "\n"
              << "  new_revision:    " << res.newRevision << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }
}

