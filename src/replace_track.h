#pragma once

#include <optional>
#include <string>

namespace crowdnoise {

// Java mapping:
// - This struct is like a Java request DTO / POJO.
struct ReplaceTrackRequest {
  // Path to immutable base JSON from decomposition team (Song_Details).
  std::string songDetailsPath;

  // Path to mutable state JSON (Song_State). Created if missing.
  std::string songStatePath;

  // Which instrument to replace, e.g. "drums".
  std::string instrumentName;

  // Filesystem path to the user's uploaded recording (mp3/wav/etc).
  std::string newTrackPath;

  // Optional optimistic locking. If provided and mismatched, the update fails.
  std::optional<int> expectedRevision;

  // Optional, for audit history.
  std::optional<std::string> userId;
};

// Java mapping:
// - This struct is like a Java response DTO / POJO.
struct ReplaceTrackResult {
  int newRevision = 0;
  std::string oldActivePath;
  std::string newActivePath;
};

// replaceTrack() updates the Song_State JSON so the chosen instrument's active_path
// now points at the new user recording. It does NOT mix/render audio.
//
// Later: this can become RemadeSong::replaceTrack(...) and songDetailsPath/songStatePath
// can become private instance variables.
ReplaceTrackResult replaceTrack(const ReplaceTrackRequest& req);

}  // namespace crowdnoise

