#include "replace_track.h"
#include "combine_audio.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "third_party/json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// Get duration (seconds) and sample rate from an audio file via ffprobe.
static bool probeAudioFile(const fs::path& path, double* outDurationSec, int* outSampleRate) {
  if (!outDurationSec || !outSampleRate) return false;
  *outDurationSec = 0.0;
  *outSampleRate = 44100;

  std::string pathStr = path.string();
  // Shell-escape for safety (minimal: handle spaces)
  std::string quoted;
  quoted.reserve(pathStr.size() + 4);
  quoted += '"';
  for (char c : pathStr) {
    if (c == '"' || c == '\\') quoted += '\\';
    quoted += c;
  }
  quoted += '"';

  // Get duration
  std::string cmd = "ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 " + quoted + " 2>/dev/null";
  FILE* fp = popen(cmd.c_str(), "r");
  if (fp) {
    char buf[128] = {0};
    if (fgets(buf, sizeof(buf), fp)) {
      double d = 0.0;
      if (sscanf(buf, "%lf", &d) == 1 && d > 0.0) *outDurationSec = d;
    }
    pclose(fp);
  }

  // Get sample rate
  cmd = "ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate -of default=nw=1:nk=1 " + quoted + " 2>/dev/null";
  fp = popen(cmd.c_str(), "r");
  if (fp) {
    char buf[128] = {0};
    if (fgets(buf, sizeof(buf), fp)) {
      int sr = 0;
      if (sscanf(buf, "%d", &sr) == 1 && sr > 0) *outSampleRate = sr;
    }
    pclose(fp);
  }

  return *outDurationSec > 0.0;
}

static std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string stripExtension(const std::string& filename) {
  const auto pos = filename.find_last_of('.');
  if (pos == std::string::npos) return filename;
  return filename.substr(0, pos);
}

// Assumes a convention like:
//   original_drums.mp3  -> drums
//   remade_drums.mp3    -> drums
static std::string instrumentFromFilename(const std::string& filenameLower) {
  std::string base = stripExtension(filenameLower);
  const std::string originalPrefix = "original_";
  const std::string remadePrefix = "remade_";

  if (base.rfind(originalPrefix, 0) == 0) return base.substr(originalPrefix.size());
  if (base.rfind(remadePrefix, 0) == 0) return base.substr(remadePrefix.size());

  throw std::runtime_error(
      "Unexpected filename format (expected original_<instrument>.mp3 or remade_<instrument>.mp3): " + filenameLower);
}

static void writeJsonFile(const fs::path& p, const json& j) {
  std::ofstream out(p, std::ios::trunc);
  if (!out.is_open()) throw std::runtime_error("Failed to open for write: " + p.string());
  out << j.dump(2) << "\n";
}

int main(int argc, char** argv) {
  try {
    const fs::path dir = (argc >= 2) ? fs::path(argv[1]) : fs::path("testing data");
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
      throw std::runtime_error("Directory not found: " + dir.string());
    }

    // 1) Discover originals + remades.
    std::map<std::string, fs::path> originalsByInstrument;
    std::map<std::string, fs::path> remadesByInstrument;

    for (const auto& entry : fs::directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;
      const std::string filename = entry.path().filename().string();
      const std::string lower = toLower(filename);
      if (!endsWith(lower, ".mp3")) continue;

      if (lower.find("original") != std::string::npos) {
        const std::string inst = instrumentFromFilename(lower);
        originalsByInstrument[inst] = entry.path();
      } else if (lower.find("remade") != std::string::npos) {
        const std::string inst = instrumentFromFilename(lower);
        remadesByInstrument[inst] = entry.path();
      }
    }

    if (originalsByInstrument.empty()) {
      throw std::runtime_error("No original_*.mp3 files found in: " + dir.string());
    }

    // 2) Probe first original for duration and sample rate.
    double durationSec = 3.0;
    int sr = 44100;
    const fs::path firstOriginal = originalsByInstrument.begin()->second;
    if (probeAudioFile(firstOriginal, &durationSec, &sr)) {
      // ok
    }
    const int songLengthMs = static_cast<int>(durationSec * 1000.0);
    if (songLengthMs <= 0) {
      throw std::runtime_error("Could not determine song duration from: " + firstOriginal.string());
    }

    // 3) Write Song_Details.json from originals.
    json details;
    details["schema_version"] = 1;
    details["song_id"] = "testing_song";
    details["title"] = "testing_song";
    details["sr"] = sr;
    details["song_length_ms"] = songLengthMs;
    details["instruments"] = json::object();

    for (const auto& [inst, path] : originalsByInstrument) {
      details["instruments"][inst] = json::object();
      // Store relative path from repo root for readability.
      details["instruments"][inst]["original_path"] = (dir / path.filename()).string();
    }

    const fs::path songDetailsPath = dir / "Song_Details.json";
    const fs::path songStatePath = dir / "Song_State.json";
    const fs::path outMixPath = dir / "combined_from_state.mp3";

    writeJsonFile(songDetailsPath, details);

    // 4) Create/update Song_State by applying replacements for any instrument that has a remade file.
    if (fs::exists(songStatePath)) fs::remove(songStatePath);

    // Sort instruments for deterministic revision order.
    std::vector<std::string> toReplace;
    for (const auto& [inst, _] : remadesByInstrument) {
      if (originalsByInstrument.count(inst)) toReplace.push_back(inst);
    }
    std::sort(toReplace.begin(), toReplace.end());

    int expectedRevision = 0;
    for (const auto& inst : toReplace) {
      crowdnoise::ReplaceTrackRequest req;
      req.songDetailsPath = songDetailsPath.string();
      req.songStatePath = songStatePath.string();
      req.instrumentName = inst;
      req.newTrackPath = (dir / remadesByInstrument[inst].filename()).string();
      req.expectedRevision = expectedRevision;
      req.userId = std::string("auto_test");
      const auto res = crowdnoise::replaceTrack(req);
      expectedRevision = res.newRevision;
    }

    // If there were no remades, still initialize a state file.
    if (!fs::exists(songStatePath)) {
      // Trigger init by doing a no-op replacement is not supported, so just copy base and set actives.
      // For this test tool, we can create a valid state by copying base layout.
      json state = details;
      state["revision"] = 0;
      state["history"] = json::array();
      for (auto& [inst, obj] : state["instruments"].items()) {
        obj["active_path"] = obj["original_path"];
        obj["active_source"] = "original";
      }
      writeJsonFile(songStatePath, state);
    }

    // 5) Mix all active tracks into one output mp3 (uses combine_audio native pipeline).
    crowdnoise::CombineAudioRequest combineReq;
    combineReq.songStatePath = songStatePath.string();
    combineReq.outMp3Path = outMixPath.string();
    combineReq.vbrQuality = 4;
    crowdnoise::combineAudio(combineReq);

    std::cout << "OK\n"
              << "  wrote: " << songDetailsPath.string() << "\n"
              << "  wrote: " << songStatePath.string() << "\n"
              << "  wrote: " << outMixPath.string() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }
}

