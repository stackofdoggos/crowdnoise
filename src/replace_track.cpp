#include "replace_track.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "third_party/json.hpp"

namespace crowdnoise {
namespace fs = std::filesystem;
using nlohmann::json;

static std::string nowIso8601Utc() {
  // Simple UTC timestamp string. (Good enough for audit history.)
  using namespace std::chrono;
  const auto now = system_clock::now();
  const std::time_t t = system_clock::to_time_t(now);

  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return std::string(buf);
}

static json readJsonFile(const fs::path& p) {
  std::ifstream in(p);
  if (!in.is_open()) {
    throw std::runtime_error("Failed to open JSON file: " + p.string());
  }
  json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    throw std::runtime_error("Failed to parse JSON file: " + p.string() + " (" + e.what() + ")");
  }
  return j;
}

static void atomicWriteJsonFile(const fs::path& dest, const json& j) {
  const fs::path tmp = dest.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.is_open()) {
      throw std::runtime_error("Failed to open temp file for write: " + tmp.string());
    }
    out << j.dump(2) << "\n";
    out.flush();
    if (!out.good()) {
      throw std::runtime_error("Failed while writing temp file: " + tmp.string());
    }
  }

  std::error_code ec;
  fs::rename(tmp, dest, ec);
  if (ec) {
    // Try to remove dest and retry rename (common on Windows, but safe elsewhere too).
    fs::remove(dest, ec);
    ec.clear();
    fs::rename(tmp, dest, ec);
    if (ec) {
      throw std::runtime_error("Failed to rename temp file into place: " + ec.message());
    }
  }
}

static void requireField(const json& j, const char* key, json::value_t type, const std::string& context) {
  if (!j.contains(key)) {
    throw std::runtime_error("Missing required field '" + std::string(key) + "' in " + context);
  }
  // nlohmann::json may represent integral numbers as signed or unsigned depending on value.
  // Treat both as acceptable when caller requests number_integer.
  if (type == json::value_t::number_integer) {
    if (!j.at(key).is_number_integer() && !j.at(key).is_number_unsigned()) {
      throw std::runtime_error("Field '" + std::string(key) + "' has wrong type in " + context);
    }
    return;
  }
  if (j.at(key).type() != type) {
    throw std::runtime_error("Field '" + std::string(key) + "' has wrong type in " + context);
  }
}

static void validateSongDetailsBase(const json& base) {
  requireField(base, "schema_version", json::value_t::number_integer, "Song_Details");
  requireField(base, "song_id", json::value_t::string, "Song_Details");
  requireField(base, "sr", json::value_t::number_integer, "Song_Details");
  requireField(base, "song_length_ms", json::value_t::number_integer, "Song_Details");
  requireField(base, "instruments", json::value_t::object, "Song_Details");

  // Validate instruments structure lightly.
  const auto& inst = base.at("instruments");
  for (auto it = inst.begin(); it != inst.end(); ++it) {
    const std::string name = it.key();
    const json& obj = it.value();
    if (!obj.is_object()) {
      throw std::runtime_error("Instrument '" + name + "' must be an object in Song_Details");
    }
    if (!obj.contains("original_path") || !obj.at("original_path").is_string()) {
      throw std::runtime_error("Instrument '" + name + "' missing string original_path in Song_Details");
    }
  }
}

static json initStateFromBase(const json& base) {
  json state = base;  // copy base content (schema_version, song_id, sr, etc.)
  state["revision"] = 0;
  state["history"] = json::array();

  // For each instrument, set active_path/source.
  for (auto& [name, obj] : state["instruments"].items()) {
    obj["active_path"] = obj["original_path"];
    obj["active_source"] = "original";
  }
  return state;
}

static void validateStateCompatibleWithBase(const json& base, const json& state) {
  // Keep this strict enough to avoid weird mismatches, but not so strict that it blocks iteration.
  if (!state.contains("song_id") || state.at("song_id") != base.at("song_id")) {
    throw std::runtime_error("Song_State song_id does not match Song_Details song_id");
  }
  if (!state.contains("sr") || state.at("sr") != base.at("sr")) {
    throw std::runtime_error("Song_State sr does not match Song_Details sr");
  }
  if (!state.contains("song_length_ms") || state.at("song_length_ms") != base.at("song_length_ms")) {
    throw std::runtime_error("Song_State song_length_ms does not match Song_Details song_length_ms");
  }
  if (!state.contains("instruments") || !state.at("instruments").is_object()) {
    throw std::runtime_error("Song_State missing instruments object");
  }
}

ReplaceTrackResult replaceTrack(const ReplaceTrackRequest& req) {
  // Validate file references / arguments
  if (req.songDetailsPath.empty()) throw std::invalid_argument("songDetailsPath is empty");
  if (req.songStatePath.empty()) throw std::invalid_argument("songStatePath is empty");
  if (req.instrumentName.empty()) throw std::invalid_argument("instrumentName is empty");
  if (req.newTrackPath.empty()) throw std::invalid_argument("newTrackPath is empty");

  const fs::path songDetailsPath(req.songDetailsPath);
  const fs::path songStatePath(req.songStatePath);
  const fs::path newTrackPath(req.newTrackPath);

  if (!fs::exists(songDetailsPath)) {
    throw std::runtime_error("Song_Details not found: " + songDetailsPath.string());
  }
  if (!fs::exists(newTrackPath)) {
    throw std::runtime_error("New track file not found: " + newTrackPath.string());
  }

  // 1) Load and validate base.
  const json base = readJsonFile(songDetailsPath);
  validateSongDetailsBase(base);

  // 2) Load or init state.
  json state;
  if (fs::exists(songStatePath)) {
    state = readJsonFile(songStatePath);
    validateStateCompatibleWithBase(base, state);
  } else {
    state = initStateFromBase(base);
    // Persist initial state immediately so later steps always operate on a real file.
    atomicWriteJsonFile(songStatePath, state);
  }

  // Ensure revision exists.
  if (!state.contains("revision") || !state.at("revision").is_number_integer()) {
    state["revision"] = 0;
  }
  const int currentRevision = state.at("revision").get<int>();

  // 3) Revision check (optimistic locking).
  if (req.expectedRevision.has_value() && req.expectedRevision.value() != currentRevision) {
    throw std::runtime_error("Revision conflict: expected " + std::to_string(req.expectedRevision.value()) +
                             " but state is " + std::to_string(currentRevision));
  }

  // 4) Validate instrument exists.
  if (!state["instruments"].contains(req.instrumentName)) {
    throw std::invalid_argument("Instrument not found: " + req.instrumentName);
  }

  json& inst = state["instruments"][req.instrumentName];
  if (!inst.contains("original_path")) {
    throw std::runtime_error("Instrument missing original_path in state: " + req.instrumentName);
  }

  // 5) Update active track pointer.
  const std::string oldActive = inst.contains("active_path") && inst["active_path"].is_string()
                                    ? inst["active_path"].get<std::string>()
                                    : inst["original_path"].get<std::string>();

  inst["active_path"] = newTrackPath.string();
  inst["active_source"] = "user";

  // Increment revision + history.
  state["revision"] = currentRevision + 1;
  if (!state.contains("history") || !state["history"].is_array()) {
    state["history"] = json::array();
  }

  json evt;
  evt["ts"] = nowIso8601Utc();
  evt["instrument"] = req.instrumentName;
  evt["old_active_path"] = oldActive;
  evt["new_active_path"] = newTrackPath.string();
  if (req.userId.has_value()) evt["user_id"] = req.userId.value();
  state["history"].push_back(evt);

  // 6) Persist atomically.
  atomicWriteJsonFile(songStatePath, state);

  ReplaceTrackResult res;
  res.newRevision = state["revision"].get<int>();
  res.oldActivePath = oldActive;
  res.newActivePath = newTrackPath.string();
  return res;
}

}  // namespace crowdnoise

