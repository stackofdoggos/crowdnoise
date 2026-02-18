#include "replace_track.h"

#include <algorithm>
#include <cctype>
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

static std::string shellQuote(const std::string& s) {
  // Minimal shell quoting for paths (handles spaces/quotes).
  std::string out = "\"";
  for (char c : s) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

static void renderMixFromSongState(const fs::path& songStatePath, const fs::path& outMp3Path) {
  std::ifstream in(songStatePath);
  if (!in.is_open()) throw std::runtime_error("Failed to open Song_State: " + songStatePath.string());
  json state;
  in >> state;

  if (!state.contains("sr") || !state["sr"].is_number()) throw std::runtime_error("Song_State missing sr");
  if (!state.contains("song_length_ms") || !state["song_length_ms"].is_number())
    throw std::runtime_error("Song_State missing song_length_ms");
  if (!state.contains("instruments") || !state["instruments"].is_object())
    throw std::runtime_error("Song_State missing instruments");

  const int sr = state["sr"].get<int>();
  const double durationSec = state["song_length_ms"].get<double>() / 1000.0;

  std::vector<std::string> inputs;
  for (auto& [name, inst] : state["instruments"].items()) {
    if (!inst.contains("active_path") || !inst["active_path"].is_string()) {
      throw std::runtime_error("Instrument '" + name + "' missing active_path in Song_State");
    }
    inputs.push_back(inst["active_path"].get<std::string>());
  }
  if (inputs.empty()) throw std::runtime_error("No instruments found to mix");

  // Build ffmpeg amix command.
  std::string cmd = "ffmpeg -hide_banner -loglevel error -y ";
  for (const auto& p : inputs) {
    cmd += "-i " + shellQuote(p) + " ";
  }
  cmd += "-filter_complex " + shellQuote("amix=inputs=" + std::to_string(inputs.size()) +
                                        ":duration=longest:dropout_transition=0") +
         " ";
  cmd += "-t " + std::to_string(durationSec) + " ";
  cmd += "-ar " + std::to_string(sr) + " -ac 1 ";
  cmd += "-q:a 4 " + shellQuote(outMp3Path.string());

  const int rc = std::system(cmd.c_str());
  if (rc != 0) throw std::runtime_error("ffmpeg mix failed with exit code: " + std::to_string(rc));
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

    // 2) Write Song_Details.json from originals.
    json details;
    details["schema_version"] = 1;
    details["song_id"] = "testing_song";
    details["title"] = "testing_song";
    details["sr"] = 44100;
    details["song_length_ms"] = 3000;  // test audio files are 3 seconds
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

    // 3) Create/update Song_State by applying replacements for any instrument that has a remade file.
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

    // 4) Mix all active tracks into one output mp3.
    renderMixFromSongState(songStatePath, outMixPath);

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

