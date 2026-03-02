#include "combine_audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "crowdnoise_native_core.h"
#include "third_party/json.hpp"

namespace crowdnoise {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct Segment {
  uint64_t startFrame = 0;
  uint64_t frameCount = 0;
};

struct InstrumentSpec {
  std::string name;
  std::string activePath;
  std::vector<Segment> segments;
};

static bool IsAbsolutePath(const std::string& p) {
  if (p.empty()) return false;
  // Unix-like absolute.
  if (p[0] == '/') return true;
  // Windows drive absolute (e.g. C:\...).
  if (p.size() >= 3 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':' &&
      (p[2] == '\\' || p[2] == '/')) {
    return true;
  }
  // UNC (\\server\share\...)
  if (p.size() >= 2 && p[0] == '\\' && p[1] == '\\') return true;
  return false;
}

static std::string ResolvePathRelativeToState(const fs::path& statePath, const std::string& p) {
  if (p.empty()) return p;
  if (IsAbsolutePath(p)) return p;

  // Prefer resolving relative to the JSON file's directory, but only if that
  // path actually exists. Some state files store paths relative to the repo
  // root (or current working directory), e.g. "testing data/foo.mp3".
  const fs::path baseDir = statePath.parent_path();
  if (!baseDir.empty()) {
    const fs::path candidate = (baseDir / fs::path(p)).lexically_normal();
    std::error_code ec;
    if (fs::exists(candidate, ec)) return candidate.string();
  }

  // Fall back to the path as-is (relative to CWD).
  return fs::path(p).lexically_normal().string();
}

static json ReadJsonFile(const fs::path& p) {
  std::ifstream in(p);
  if (!in.is_open()) {
    throw std::runtime_error("Failed to open JSON file: " + p.string());
  }
  json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    throw std::runtime_error("Failed to parse JSON file: " + p.string() + " (" + std::string(e.what()) + ")");
  }
  return j;
}

static uint64_t MsToFrames(double ms) {
  // Our portable pipeline standardizes to 48kHz stereo, so:
  // frames = seconds * 48000 = (ms/1000) * 48000 = ms * 48.
  if (!(ms > 0.0)) return 0;
  const double frames = ms * 48.0;
  const long long rounded = std::llround(frames);
  return (rounded <= 0) ? 0u : static_cast<uint64_t>(rounded);
}

static double ReadNumber(const json& j, const char* key, const std::string& ctx) {
  if (!j.contains(key)) throw std::runtime_error("Missing '" + std::string(key) + "' in " + ctx);
  const json& v = j.at(key);
  if (!v.is_number()) throw std::runtime_error("Field '" + std::string(key) + "' must be number in " + ctx);
  return v.get<double>();
}

static std::string ReadString(const json& j, const char* key, const std::string& ctx) {
  if (!j.contains(key)) throw std::runtime_error("Missing '" + std::string(key) + "' in " + ctx);
  const json& v = j.at(key);
  if (!v.is_string()) throw std::runtime_error("Field '" + std::string(key) + "' must be string in " + ctx);
  return v.get<std::string>();
}

static void AddViewToMixOrThrow(const crowdnoise_pcm_f32_t& track,
                               uint64_t trackStartFrame,
                               uint64_t frameCount,
                               uint64_t mixStartFrame,
                               crowdnoise_pcm_f32_t* mix) {
  if (!mix) throw std::runtime_error("Internal error: mix is null");
  if (frameCount == 0) return;
  if (!track.pcmInterleaved) throw std::runtime_error("Internal error: track pcm buffer is null");

  if (track.sampleRate != 48000 || track.channels != 2) {
    throw std::runtime_error("Internal error: track must be 48kHz stereo");
  }
  if (mix->sampleRate != 48000 || mix->channels != 2) {
    throw std::runtime_error("Internal error: mix must be 48kHz stereo");
  }

  if (trackStartFrame > track.frameCount) return;
  const uint64_t remaining = track.frameCount - trackStartFrame;
  const uint64_t use = std::min<uint64_t>(frameCount, remaining);
  if (use == 0) return;

  const uint64_t off64 = trackStartFrame * 2u;
  if (off64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    throw std::runtime_error("Track pointer offset too large");
  }
  const size_t off = static_cast<size_t>(off64);

  crowdnoise_pcm_f32_t view{};
  view.sampleRate = 48000;
  view.channels = 2;
  view.frameCount = use;
  view.pcmInterleaved = track.pcmInterleaved + off;

  char err[512];
  std::memset(err, 0, sizeof(err));
  const crowdnoise_result_t rc = crowdnoise_step5_add_track_to_mix(&view, mixStartFrame, mix, err, sizeof(err));
  if (rc != CROWDNOISE_OK) {
    throw std::runtime_error(std::string("Step 5 add_track_to_mix failed: ") + (err[0] ? err : "unknown error"));
  }
}

}  // namespace

CombineAudioResult combineAudio(const CombineAudioRequest& req) {
  if (req.songStatePath.empty()) throw std::invalid_argument("songStatePath is empty");
  if (req.outMp3Path.empty()) throw std::invalid_argument("outMp3Path is empty");
  if (req.vbrQuality < 0 || req.vbrQuality > 9) throw std::invalid_argument("vbrQuality must be in [0, 9]");

  const fs::path songStatePath(req.songStatePath);
  if (!fs::exists(songStatePath)) {
    throw std::runtime_error("Song_State not found: " + songStatePath.string());
  }

  const json state = ReadJsonFile(songStatePath);
  if (!state.is_object()) throw std::runtime_error("Song_State root must be a JSON object");

  const double songLenMs = ReadNumber(state, "song_length_ms", "Song_State");
  if (!(songLenMs > 0.0)) throw std::runtime_error("Song_State song_length_ms must be > 0");
  const uint64_t songFrames = MsToFrames(songLenMs);
  if (songFrames == 0) throw std::runtime_error("Song_State song_length_ms converts to 0 frames");

  if (!state.contains("instruments") || !state.at("instruments").is_object()) {
    throw std::runtime_error("Song_State missing instruments object");
  }

  std::vector<InstrumentSpec> instruments;
  instruments.reserve(state.at("instruments").size());
  for (auto it = state.at("instruments").begin(); it != state.at("instruments").end(); ++it) {
    const std::string name = it.key();
    const json& instObj = it.value();
    if (!instObj.is_object()) continue;

    InstrumentSpec inst;
    inst.name = name;
    inst.activePath = ReadString(instObj, "active_path", "Song_State instruments." + name);

    // Optional segments.
    if (instObj.contains("segments")) {
      const json& segs = instObj.at("segments");
      if (!segs.is_array()) {
        throw std::runtime_error("Instrument '" + name + "' segments must be an array");
      }
      for (size_t idx = 0; idx < segs.size(); ++idx) {
        const json& seg = segs.at(idx);
        if (!seg.is_object()) {
          throw std::runtime_error("Instrument '" + name + "' segment must be an object");
        }
        const double startMs = ReadNumber(seg, "start_ms", "segments[" + std::to_string(idx) + "]");
        const double endMs = ReadNumber(seg, "end_ms", "segments[" + std::to_string(idx) + "]");
        if (!(startMs >= 0.0) || !(endMs > startMs)) {
          throw std::runtime_error("Instrument '" + name + "' segment has invalid range");
        }
        if (endMs > songLenMs + 1e-9) {
          throw std::runtime_error("Instrument '" + name + "' segment exceeds song_length_ms");
        }
        const uint64_t startF = MsToFrames(startMs);
        const uint64_t endF = MsToFrames(endMs);
        if (endF <= startF) continue;
        if (startF >= songFrames) continue;
        const uint64_t clampedEnd = std::min<uint64_t>(endF, songFrames);
        if (clampedEnd <= startF) continue;
        Segment s;
        s.startFrame = startF;
        s.frameCount = clampedEnd - startF;
        inst.segments.push_back(s);
      }
    }

    instruments.push_back(std::move(inst));
  }

  if (instruments.empty()) throw std::runtime_error("Song_State has no instruments to mix");

  // 1) Decode+standardize each instrument active_path to 48kHz stereo float PCM.
  std::vector<crowdnoise_pcm_f32_t> tracks(instruments.size());
  for (size_t i = 0; i < instruments.size(); ++i) {
    std::memset(&tracks[i], 0, sizeof(crowdnoise_pcm_f32_t));
    const std::string resolved = ResolvePathRelativeToState(songStatePath, instruments[i].activePath);

    char err[512];
    std::memset(err, 0, sizeof(err));
    const crowdnoise_result_t rc = crowdnoise_decode_standardize_mp3_file(
        resolved.c_str(), &tracks[i], err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      for (size_t j = 0; j < i; ++j) crowdnoise_free_pcm_f32(&tracks[j]);
      throw std::runtime_error("Decode+standardize failed for instrument '" + instruments[i].name + "' (" + resolved +
                               "): " + (err[0] ? std::string(err) : "unknown error"));
    }
  }

  // 2) Allocate the final mix timeline (exactly song_length_ms frames).
  crowdnoise_pcm_f32_t mix{};
  std::memset(&mix, 0, sizeof(mix));
  {
    crowdnoise_track_placement_t placement{0u, songFrames};
    char err[512];
    std::memset(err, 0, sizeof(err));
    const crowdnoise_result_t rc = crowdnoise_step4_allocate_silent_mix_buffer(&placement, 1, &mix, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      for (auto& t : tracks) crowdnoise_free_pcm_f32(&t);
      throw std::runtime_error(std::string("Step 4 allocate mix buffer failed: ") + (err[0] ? err : "unknown error"));
    }
  }

  // 3) Mix instruments.
  try {
    for (size_t i = 0; i < instruments.size(); ++i) {
      const auto& inst = instruments[i];
      const auto& track = tracks[i];

      if (track.frameCount == 0) continue;

      if (inst.segments.empty()) {
        // Full-stem mode: mix from t=0, clamped to song length.
        const uint64_t frameCount = std::min<uint64_t>(track.frameCount, songFrames);
        AddViewToMixOrThrow(track, /*trackStartFrame=*/0u, frameCount, /*mixStartFrame=*/0u, &mix);
        continue;
      }

      // Segments mode.
      //
      // If the active track is at least song-length, treat it as timeline-aligned and
      // mix seg windows at their same offsets.
      //
      // If the active track is shorter than the song, treat it as a "clip" and
      // fill segment windows sequentially from the start of the clip.
      if (track.frameCount >= songFrames) {
        for (const auto& seg : inst.segments) {
          if (seg.frameCount == 0) continue;
          if (seg.startFrame >= songFrames) continue;

          const uint64_t maxToSong = songFrames - seg.startFrame;
          const uint64_t use = std::min<uint64_t>(seg.frameCount, maxToSong);
          AddViewToMixOrThrow(track, /*trackStartFrame=*/seg.startFrame, use, /*mixStartFrame=*/seg.startFrame, &mix);
        }
      } else {
        uint64_t srcCursor = 0;
        for (const auto& seg : inst.segments) {
          if (seg.frameCount == 0) continue;
          if (seg.startFrame >= songFrames) continue;
          if (srcCursor >= track.frameCount) break;

          const uint64_t maxToSong = songFrames - seg.startFrame;
          const uint64_t maxToSrc = track.frameCount - srcCursor;
          const uint64_t use = std::min<uint64_t>(seg.frameCount, std::min<uint64_t>(maxToSong, maxToSrc));
          AddViewToMixOrThrow(track, /*trackStartFrame=*/srcCursor, use, /*mixStartFrame=*/seg.startFrame, &mix);
          srcCursor += use;
        }
      }
    }
  } catch (...) {
    for (auto& t : tracks) crowdnoise_free_pcm_f32(&t);
    crowdnoise_free_pcm_f32(&mix);
    throw;
  }

  // 4) Step 6: prevent clipping (constant scale).
  float maxAbs = 0.0f;
  float scaleApplied = 1.0f;
  {
    char err[512];
    std::memset(err, 0, sizeof(err));
    const crowdnoise_result_t rc = crowdnoise_step6_prevent_clipping_in_place(&mix, &maxAbs, &scaleApplied, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      for (auto& t : tracks) crowdnoise_free_pcm_f32(&t);
      crowdnoise_free_pcm_f32(&mix);
      throw std::runtime_error(std::string("Step 6 prevent clipping failed: ") + (err[0] ? err : "unknown error"));
    }
  }

  // 5) Step 7: encode to MP3.
  {
    char err[512];
    std::memset(err, 0, sizeof(err));
    const crowdnoise_result_t rc = crowdnoise_step7_encode_mix_to_mp3_file(
        &mix, req.outMp3Path.c_str(), req.vbrQuality, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      for (auto& t : tracks) crowdnoise_free_pcm_f32(&t);
      crowdnoise_free_pcm_f32(&mix);
      throw std::runtime_error(std::string("Step 7 encode MP3 failed: ") + (err[0] ? err : "unknown error"));
    }
  }

  // Cleanup decoded buffers.
  for (auto& t : tracks) crowdnoise_free_pcm_f32(&t);
  crowdnoise_free_pcm_f32(&mix);

  CombineAudioResult res;
  res.maxAbs = maxAbs;
  res.scaleApplied = scaleApplied;
  res.songFrames = songFrames;
  return res;
}

}  // namespace crowdnoise

