// Mix a song described by a JSON file (timeline placement).
//
// This is a CLI wrapper around the existing Steps 1-7 in crowdnoise_native_core:
// - Reads a project JSON (see testFileFromPersonA.json)
// - For each instrument:
//   - decode+standardize its active_path to 48kHz stereo float PCM
//   - mix ONLY the listed [start_ms, end_ms] segments at those exact timestamps
// - Prevent clipping via Step 6 (constant scale), then encode to MP3 via Step 7.
//
// Usage:
//   mix_json_cli SONG.json OUT.mp3
//
// Example:
//   mix_json_cli testFileFromPersonA.json out.mp3
//
// Notes:
// - This intentionally does NOT (yet) fall back to original_path outside segments.
//   If you want "use original outside segments", we can extend the JSON rules.

#include "crowdnoise_native_core.h"

#include <algorithm>
#include <cmath>   // llround, isfinite
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// -------------------------
// Minimal JSON parser (just enough for our schema).
// -------------------------

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool b = false;
  double n = 0.0;
  std::string s;
  std::vector<Json> a;
  std::map<std::string, Json> o;

  static Json MakeNull() { return Json{}; }
  static Json MakeBool(bool v) {
    Json j;
    j.type = Type::Bool;
    j.b = v;
    return j;
  }
  static Json MakeNumber(double v) {
    Json j;
    j.type = Type::Number;
    j.n = v;
    return j;
  }
  static Json MakeString(std::string v) {
    Json j;
    j.type = Type::String;
    j.s = std::move(v);
    return j;
  }
  static Json MakeArray(std::vector<Json> v) {
    Json j;
    j.type = Type::Array;
    j.a = std::move(v);
    return j;
  }
  static Json MakeObject(std::map<std::string, Json> v) {
    Json j;
    j.type = Type::Object;
    j.o = std::move(v);
    return j;
  }
};

struct JsonParseError {
  std::string message;
  size_t offset = 0;
};

static inline void SkipWs(const std::string& in, size_t* i) {
  while (*i < in.size() && std::isspace(static_cast<unsigned char>(in[*i]))) ++(*i);
}

static bool ParseValue(const std::string& in, size_t* i, Json* out, JsonParseError* err, int depth);

static bool ParseLiteral(const std::string& in, size_t* i, const char* lit) {
  const size_t n = std::strlen(lit);
  if (*i + n > in.size()) return false;
  if (in.compare(*i, n, lit) != 0) return false;
  *i += n;
  return true;
}

static int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static void AppendUtf8(uint32_t cp, std::string* out) {
  if (!out) return;
  if (cp <= 0x7Fu) {
    out->push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out->push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out->push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out->push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out->push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

static bool ParseString(const std::string& in, size_t* i, std::string* out, JsonParseError* err) {
  if (*i >= in.size() || in[*i] != '"') {
    if (err) {
      err->message = "Expected '\"' to start string";
      err->offset = *i;
    }
    return false;
  }
  ++(*i);
  std::string s;
  while (*i < in.size()) {
    const char c = in[*i];
    if (c == '"') {
      ++(*i);
      *out = std::move(s);
      return true;
    }
    if (static_cast<unsigned char>(c) < 0x20) {
      if (err) {
        err->message = "Control character in string";
        err->offset = *i;
      }
      return false;
    }
    if (c != '\\') {
      s.push_back(c);
      ++(*i);
      continue;
    }
    // Escape sequence.
    ++(*i);
    if (*i >= in.size()) {
      if (err) {
        err->message = "Unterminated escape sequence";
        err->offset = *i;
      }
      return false;
    }
    const char e = in[*i];
    ++(*i);
    switch (e) {
      case '"':
      case '\\':
      case '/':
        s.push_back(e);
        break;
      case 'b':
        s.push_back('\b');
        break;
      case 'f':
        s.push_back('\f');
        break;
      case 'n':
        s.push_back('\n');
        break;
      case 'r':
        s.push_back('\r');
        break;
      case 't':
        s.push_back('\t');
        break;
      case 'u': {
        if (*i + 4 > in.size()) {
          if (err) {
            err->message = "Invalid \\u escape";
            err->offset = *i;
          }
          return false;
        }
        uint32_t cp = 0;
        for (int k = 0; k < 4; ++k) {
          const int hv = HexVal(in[*i + static_cast<size_t>(k)]);
          if (hv < 0) {
            if (err) {
              err->message = "Invalid hex digit in \\u escape";
              err->offset = *i + static_cast<size_t>(k);
            }
            return false;
          }
          cp = (cp << 4) | static_cast<uint32_t>(hv);
        }
        *i += 4;
        // Note: We do not implement surrogate pair decoding here; not needed for our schema.
        AppendUtf8(cp, &s);
        break;
      }
      default:
        if (err) {
          err->message = "Invalid escape character";
          err->offset = (*i > 0) ? (*i - 1) : 0;
        }
        return false;
    }
  }
  if (err) {
    err->message = "Unterminated string";
    err->offset = *i;
  }
  return false;
}

static bool ParseNumber(const std::string& in, size_t* i, double* out, JsonParseError* err) {
  const size_t start = *i;
  if (*i < in.size() && (in[*i] == '-' || in[*i] == '+')) ++(*i);
  bool any = false;
  while (*i < in.size() && std::isdigit(static_cast<unsigned char>(in[*i]))) {
    any = true;
    ++(*i);
  }
  if (*i < in.size() && in[*i] == '.') {
    ++(*i);
    while (*i < in.size() && std::isdigit(static_cast<unsigned char>(in[*i]))) {
      any = true;
      ++(*i);
    }
  }
  if (*i < in.size() && (in[*i] == 'e' || in[*i] == 'E')) {
    ++(*i);
    if (*i < in.size() && (in[*i] == '-' || in[*i] == '+')) ++(*i);
    bool expAny = false;
    while (*i < in.size() && std::isdigit(static_cast<unsigned char>(in[*i]))) {
      expAny = true;
      ++(*i);
    }
    if (!expAny) {
      if (err) {
        err->message = "Invalid exponent in number";
        err->offset = *i;
      }
      return false;
    }
  }
  if (!any) {
    if (err) {
      err->message = "Invalid number";
      err->offset = start;
    }
    return false;
  }
  const std::string sub = in.substr(start, *i - start);
  char* endp = nullptr;
  const double v = std::strtod(sub.c_str(), &endp);
  if (!endp || *endp != '\0' || !std::isfinite(v)) {
    if (err) {
      err->message = "Failed to parse number";
      err->offset = start;
    }
    return false;
  }
  *out = v;
  return true;
}

static bool ParseArray(const std::string& in, size_t* i, Json* out, JsonParseError* err, int depth) {
  if (*i >= in.size() || in[*i] != '[') {
    if (err) {
      err->message = "Expected '['";
      err->offset = *i;
    }
    return false;
  }
  ++(*i);
  SkipWs(in, i);
  std::vector<Json> arr;
  if (*i < in.size() && in[*i] == ']') {
    ++(*i);
    *out = Json::MakeArray(std::move(arr));
    return true;
  }
  for (;;) {
    SkipWs(in, i);
    Json v;
    if (!ParseValue(in, i, &v, err, depth + 1)) return false;
    arr.push_back(std::move(v));
    SkipWs(in, i);
    if (*i >= in.size()) break;
    if (in[*i] == ',') {
      ++(*i);
      continue;
    }
    if (in[*i] == ']') {
      ++(*i);
      *out = Json::MakeArray(std::move(arr));
      return true;
    }
    break;
  }
  if (err) {
    err->message = "Expected ',' or ']' in array";
    err->offset = *i;
  }
  return false;
}

static bool ParseObject(const std::string& in, size_t* i, Json* out, JsonParseError* err, int depth) {
  if (*i >= in.size() || in[*i] != '{') {
    if (err) {
      err->message = "Expected '{'";
      err->offset = *i;
    }
    return false;
  }
  ++(*i);
  SkipWs(in, i);
  std::map<std::string, Json> obj;
  if (*i < in.size() && in[*i] == '}') {
    ++(*i);
    *out = Json::MakeObject(std::move(obj));
    return true;
  }
  for (;;) {
    SkipWs(in, i);
    std::string key;
    if (!ParseString(in, i, &key, err)) return false;
    SkipWs(in, i);
    if (*i >= in.size() || in[*i] != ':') {
      if (err) {
        err->message = "Expected ':' after object key";
        err->offset = *i;
      }
      return false;
    }
    ++(*i);
    SkipWs(in, i);
    Json v;
    if (!ParseValue(in, i, &v, err, depth + 1)) return false;
    obj.emplace(std::move(key), std::move(v));
    SkipWs(in, i);
    if (*i >= in.size()) break;
    if (in[*i] == ',') {
      ++(*i);
      continue;
    }
    if (in[*i] == '}') {
      ++(*i);
      *out = Json::MakeObject(std::move(obj));
      return true;
    }
    break;
  }
  if (err) {
    err->message = "Expected ',' or '}' in object";
    err->offset = *i;
  }
  return false;
}

static bool ParseValue(const std::string& in, size_t* i, Json* out, JsonParseError* err, int depth) {
  if (depth > 256) {
    if (err) {
      err->message = "JSON nesting too deep";
      err->offset = *i;
    }
    return false;
  }
  SkipWs(in, i);
  if (*i >= in.size()) {
    if (err) {
      err->message = "Unexpected end of input";
      err->offset = *i;
    }
    return false;
  }
  const char c = in[*i];
  if (c == '{') return ParseObject(in, i, out, err, depth);
  if (c == '[') return ParseArray(in, i, out, err, depth);
  if (c == '"') {
    std::string s;
    if (!ParseString(in, i, &s, err)) return false;
    *out = Json::MakeString(std::move(s));
    return true;
  }
  if (c == 'n') {
    if (!ParseLiteral(in, i, "null")) {
      if (err) {
        err->message = "Invalid literal";
        err->offset = *i;
      }
      return false;
    }
    *out = Json::MakeNull();
    return true;
  }
  if (c == 't') {
    if (!ParseLiteral(in, i, "true")) {
      if (err) {
        err->message = "Invalid literal";
        err->offset = *i;
      }
      return false;
    }
    *out = Json::MakeBool(true);
    return true;
  }
  if (c == 'f') {
    if (!ParseLiteral(in, i, "false")) {
      if (err) {
        err->message = "Invalid literal";
        err->offset = *i;
      }
      return false;
    }
    *out = Json::MakeBool(false);
    return true;
  }
  // Number.
  if (c == '-' || c == '+' || std::isdigit(static_cast<unsigned char>(c))) {
    double v = 0.0;
    if (!ParseNumber(in, i, &v, err)) return false;
    *out = Json::MakeNumber(v);
    return true;
  }
  if (err) {
    err->message = "Unexpected token";
    err->offset = *i;
  }
  return false;
}

static bool ParseJson(const std::string& in, Json* out, JsonParseError* err) {
  size_t i = 0;
  if (!ParseValue(in, &i, out, err, /*depth=*/0)) return false;
  SkipWs(in, &i);
  if (i != in.size()) {
    if (err) {
      err->message = "Trailing characters after JSON value";
      err->offset = i;
    }
    return false;
  }
  return true;
}

static bool ReadFileToString(const std::string& path, std::string* out, std::string* err) {
  if (!out) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err) *err = "Failed to open file: " + path;
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

// -------------------------
// Song schema extraction
// -------------------------

struct Segment {
  uint64_t startFrame = 0;
  uint64_t frameCount = 0;
};

struct Instrument {
  std::string name;
  // JSON fields:
  // - active_path: by default the "replacement" or active track to mix.
  // - original_path: optional; if provided and segments are absent, we derive where the
  //   instrument should be audible by analyzing this original stem.
  // - replacement_path: optional; if present, overrides active_path for replacement audio.
  std::string activePath;
  std::string originalPath;
  std::string replacementPath;
  std::vector<Segment> segments;
};

static bool JsonGetNumber(const Json& j, const char* key, double* out) {
  if (!out) return false;
  if (j.type != Json::Type::Object) return false;
  auto it = j.o.find(key);
  if (it == j.o.end()) return false;
  if (it->second.type != Json::Type::Number) return false;
  *out = it->second.n;
  return true;
}

static bool JsonGetString(const Json& j, const char* key, std::string* out) {
  if (!out) return false;
  if (j.type != Json::Type::Object) return false;
  auto it = j.o.find(key);
  if (it == j.o.end()) return false;
  if (it->second.type != Json::Type::String) return false;
  *out = it->second.s;
  return true;
}

static bool JsonGetStringOptional(const Json& j, const char* key, std::string* out) {
  if (!out) return false;
  if (j.type != Json::Type::Object) return false;
  auto it = j.o.find(key);
  if (it == j.o.end()) return false;
  if (it->second.type != Json::Type::String) return false;
  *out = it->second.s;
  return true;
}

static const Json* JsonGetObjectMember(const Json& j, const char* key) {
  if (j.type != Json::Type::Object) return nullptr;
  auto it = j.o.find(key);
  if (it == j.o.end()) return nullptr;
  return &it->second;
}

static uint64_t MsToFrames(double ms) {
  // Our pipeline standardizes to 48kHz, so:
  // frames = seconds * 48000 = (ms/1000) * 48000 = ms * 48.
  if (!(ms > 0.0)) return 0;
  const double frames = ms * 48.0;
  const long long rounded = std::llround(frames);
  return (rounded <= 0) ? 0u : static_cast<uint64_t>(rounded);
}

static bool LoadSongFromJson(const Json& root, uint64_t* outSongFrames, std::vector<Instrument>* outInstruments, std::string* err) {
  if (!outSongFrames || !outInstruments) return false;
  outInstruments->clear();
  *outSongFrames = 0;

  if (root.type != Json::Type::Object) {
    if (err) *err = "Root JSON must be an object";
    return false;
  }

  double songLenMs = 0.0;
  if (!JsonGetNumber(root, "song_length_ms", &songLenMs)) {
    if (err) *err = "Missing/invalid song_length_ms";
    return false;
  }
  if (!(songLenMs > 0.0)) {
    if (err) *err = "song_length_ms must be > 0";
    return false;
  }
  const uint64_t songFrames = MsToFrames(songLenMs);
  if (songFrames == 0) {
    if (err) *err = "song_length_ms converted to 0 frames";
    return false;
  }
  *outSongFrames = songFrames;

  const Json* instruments = JsonGetObjectMember(root, "instruments");
  if (!instruments || instruments->type != Json::Type::Object) {
    if (err) *err = "Missing/invalid instruments object";
    return false;
  }

  for (const auto& kv : instruments->o) {
    const std::string& instrumentName = kv.first;
    const Json& instObj = kv.second;
    if (instObj.type != Json::Type::Object) continue;

    Instrument inst;
    inst.name = instrumentName;
    if (!JsonGetString(instObj, "active_path", &inst.activePath) || inst.activePath.empty()) {
      if (err) *err = "Instrument '" + instrumentName + "' missing/invalid active_path";
      return false;
    }

    // Optional fields for the "mask from original" mode.
    (void)JsonGetStringOptional(instObj, "original_path", &inst.originalPath);
    (void)JsonGetStringOptional(instObj, "replacement_path", &inst.replacementPath);

    // segments are optional; if present, we interpret them as explicit audible windows.
    const Json* segs = JsonGetObjectMember(instObj, "segments");
    if (segs) {
      if (segs->type != Json::Type::Array) {
        if (err) *err = "Instrument '" + instrumentName + "' has invalid segments (must be array)";
        return false;
      }
      for (size_t idx = 0; idx < segs->a.size(); ++idx) {
        const Json& segObj = segs->a[idx];
        if (segObj.type != Json::Type::Object) {
          if (err) *err = "Instrument '" + instrumentName + "' has non-object segment";
          return false;
        }
        double startMs = 0.0, endMs = 0.0;
        if (!JsonGetNumber(segObj, "start_ms", &startMs) || !JsonGetNumber(segObj, "end_ms", &endMs)) {
          if (err) *err = "Instrument '" + instrumentName + "' segment missing start_ms/end_ms";
          return false;
        }
        if (!(startMs >= 0.0) || !(endMs >= 0.0) || !(endMs > startMs)) {
          if (err) *err = "Instrument '" + instrumentName + "' segment has invalid range";
          return false;
        }
        const uint64_t startF = MsToFrames(startMs);
        const uint64_t endF = MsToFrames(endMs);
        if (endF <= startF) {
          if (err) *err = "Instrument '" + instrumentName + "' segment converts to empty frame range";
          return false;
        }
        if (endF > songFrames) {
          if (err) *err = "Instrument '" + instrumentName + "' segment exceeds song_length_ms";
          return false;
        }
        Segment seg;
        seg.startFrame = startF;
        seg.frameCount = endF - startF;
        inst.segments.push_back(seg);
      }
    }

    outInstruments->push_back(std::move(inst));
  }

  if (outInstruments->empty()) {
    if (err) *err = "No instruments found in JSON";
    return false;
  }

  return true;
}

static void PrintUsage(const char* exe) {
  std::cerr << "Usage:\n"
            << "  " << exe << " SONG.json OUT.mp3\n"
            << "\n"
            << "Example:\n"
            << "  " << exe << " testFileFromPersonA.json out.mp3\n";
}

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

static std::string Dirname(const std::string& path) {
  // Handles both '/' and '\'.
  const size_t pos1 = path.find_last_of('/');
  const size_t pos2 = path.find_last_of('\\');
  const size_t pos = (pos1 == std::string::npos) ? pos2
                   : (pos2 == std::string::npos) ? pos1
                   : std::max(pos1, pos2);
  if (pos == std::string::npos) return std::string();
  return path.substr(0, pos);
}

static std::string JoinPath(const std::string& dir, const std::string& rel) {
  if (dir.empty()) return rel;
  if (rel.empty()) return dir;
  const char last = dir.back();
  if (last == '/' || last == '\\') return dir + rel;
  return dir + "/" + rel;
}

static std::string ResolvePathRelativeToJson(const std::string& jsonDir, const std::string& p) {
  if (p.empty()) return p;
  return IsAbsolutePath(p) ? p : JoinPath(jsonDir, p);
}

struct GateParams {
  uint32_t windowFrames = 960;  // 20ms at 48kHz
  // Thresholds are linear amplitude (float PCM). These correspond roughly to:
  // - onThreshold ~ -44 dBFS
  // - offThreshold ~ -48 dBFS
  float onThreshold = 0.006f;
  float offThreshold = 0.004f;
  uint32_t attackWindows = 2;   // require N windows above onThreshold
  uint32_t releaseWindows = 4;  // require N windows below offThreshold
};

static float RmsWindowStereo(const crowdnoise_pcm_f32_t& pcm, uint64_t startFrame, uint64_t endFrame) {
  if (!pcm.pcmInterleaved || pcm.channels != 2) return 0.0f;
  if (endFrame <= startFrame) return 0.0f;
  const uint64_t frames = endFrame - startFrame;
  const float* x = pcm.pcmInterleaved + static_cast<size_t>(startFrame * 2u);
  long double sum = 0.0L;
  const size_t samples = static_cast<size_t>(frames * 2u);
  for (size_t i = 0; i < samples; ++i) {
    const float v = x[i];
    sum += static_cast<long double>(v) * static_cast<long double>(v);
  }
  const long double mean = sum / static_cast<long double>(samples);
  const long double r = std::sqrt(mean);
  return static_cast<float>(r);
}

static void RenderMaskedReplacementStem(const crowdnoise_pcm_f32_t& original,
                                       const crowdnoise_pcm_f32_t& replacement,
                                       uint64_t songFrames,
                                       const GateParams& p,
                                       std::vector<float>* outRenderedInterleaved) {
  outRenderedInterleaved->assign(static_cast<size_t>(songFrames * 2u), 0.0f);
  if (!original.pcmInterleaved || !replacement.pcmInterleaved) return;
  if (original.sampleRate != 48000 || original.channels != 2) return;
  if (replacement.sampleRate != 48000 || replacement.channels != 2) return;
  if (songFrames == 0) return;
  if (replacement.frameCount == 0) return;

  const uint64_t maskFrames = std::min<uint64_t>(songFrames, original.frameCount);
  const uint32_t W = (p.windowFrames == 0) ? 1u : p.windowFrames;

  bool stateOn = false;
  uint32_t onCount = 0;
  uint32_t offCount = 0;
  uint64_t repPos = 0;  // in frames; advances only during ON windows

  for (uint64_t wStart = 0; wStart < maskFrames; wStart += W) {
    const uint64_t wEnd = std::min<uint64_t>(maskFrames, wStart + static_cast<uint64_t>(W));
    const float rms = RmsWindowStereo(original, wStart, wEnd);

    if (!stateOn) {
      if (rms >= p.onThreshold) {
        onCount++;
      } else {
        onCount = 0;
      }
      if (onCount >= p.attackWindows) {
        stateOn = true;
        offCount = 0;
      }
    } else {
      if (rms < p.offThreshold) {
        offCount++;
      } else {
        offCount = 0;
      }
      if (offCount >= p.releaseWindows) {
        stateOn = false;
        onCount = 0;
      }
    }

    if (!stateOn) continue;

    // Fill rendered[wStart:wEnd) with replacement frames, looping as needed.
    for (uint64_t f = wStart; f < wEnd; ++f) {
      const uint64_t srcF = (replacement.frameCount == 0) ? 0u : (repPos % replacement.frameCount);
      const size_t dstIdx = static_cast<size_t>(f * 2u);
      const size_t srcIdx = static_cast<size_t>(srcF * 2u);
      (*outRenderedInterleaved)[dstIdx + 0] = replacement.pcmInterleaved[srcIdx + 0];
      (*outRenderedInterleaved)[dstIdx + 1] = replacement.pcmInterleaved[srcIdx + 1];
      repPos++;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string jsonPath = argv[1];
  const std::string outMp3Path = argv[2];

  // 1) Read + parse JSON.
  std::string jsonText;
  std::string ioErr;
  if (!ReadFileToString(jsonPath, &jsonText, &ioErr)) {
    std::cerr << ioErr << "\n";
    return 1;
  }

  Json root;
  JsonParseError perr;
  if (!ParseJson(jsonText, &root, &perr)) {
    std::cerr << "JSON parse error: " << perr.message << " (offset " << perr.offset << ")\n";
    return 1;
  }

  uint64_t songFrames = 0;
  std::vector<Instrument> instruments;
  std::string schemaErr;
  if (!LoadSongFromJson(root, &songFrames, &instruments, &schemaErr)) {
    std::cerr << "JSON schema error: " << schemaErr << "\n";
    return 1;
  }

  // 2) Decode+standardize tracks.
  const std::string jsonDir = Dirname(jsonPath);
  std::vector<crowdnoise_pcm_f32_t> replacementTracks(instruments.size());
  std::vector<crowdnoise_pcm_f32_t> originalTracks(instruments.size());
  std::vector<bool> hasOriginal(instruments.size(), false);
  char err[512];

  for (size_t i = 0; i < instruments.size(); ++i) {
    std::memset(err, 0, sizeof(err));
    std::memset(&replacementTracks[i], 0, sizeof(crowdnoise_pcm_f32_t));
    std::memset(&originalTracks[i], 0, sizeof(crowdnoise_pcm_f32_t));

    const std::string replacementPath = ResolvePathRelativeToJson(
        jsonDir, instruments[i].replacementPath.empty() ? instruments[i].activePath : instruments[i].replacementPath);

    const crowdnoise_result_t rc = crowdnoise_decode_standardize_mp3_file(
        replacementPath.c_str(), &replacementTracks[i], err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Decode+standardize failed for instrument '" << instruments[i].name
                << "' (" << replacementPath << "): " << err << "\n";
      for (size_t j = 0; j < i; ++j) {
        crowdnoise_free_pcm_f32(&replacementTracks[j]);
        if (hasOriginal[j]) crowdnoise_free_pcm_f32(&originalTracks[j]);
      }
      return 1;
    }

    if (!instruments[i].originalPath.empty() && instruments[i].segments.empty()) {
      const std::string origPath = ResolvePathRelativeToJson(jsonDir, instruments[i].originalPath);
      std::memset(err, 0, sizeof(err));
      const crowdnoise_result_t rc2 = crowdnoise_decode_standardize_mp3_file(
          origPath.c_str(), &originalTracks[i], err, sizeof(err));
      if (rc2 != CROWDNOISE_OK) {
        std::cerr << "Decode+standardize failed for original stem of instrument '" << instruments[i].name
                  << "' (" << origPath << "): " << err << "\n";
        for (size_t j = 0; j <= i; ++j) {
          crowdnoise_free_pcm_f32(&replacementTracks[j]);
          if (hasOriginal[j]) crowdnoise_free_pcm_f32(&originalTracks[j]);
        }
        return 1;
      }
      hasOriginal[i] = true;
    }
  }

  // 3) Allocate the final mix timeline.
  // Always allocate exactly song_length_ms.
  crowdnoise_track_placement_t placement{0u, songFrames};

  crowdnoise_pcm_f32_t mix{};
  std::memset(err, 0, sizeof(err));
  {
    const crowdnoise_result_t rc = crowdnoise_step4_allocate_silent_mix_buffer(
        &placement, 1, &mix, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 4 allocate failed: " << err << "\n";
      for (size_t i = 0; i < instruments.size(); ++i) {
        crowdnoise_free_pcm_f32(&replacementTracks[i]);
        if (hasOriginal[i]) crowdnoise_free_pcm_f32(&originalTracks[i]);
      }
      return 1;
    }
  }

  // 4) Mix instruments.
  //    - If segments exist: mix only those audible windows (legacy mode).
  //    - Else if original_path exists: derive an audible mask from the original stem and render a full-length stem.
  //    - Else: mix the entire replacement track from t=0, clamped to song length.
  std::vector<std::vector<float>> renderedStems;  // keeps storage alive for stem views
  renderedStems.reserve(instruments.size());
  const GateParams gateParams{};

  for (size_t i = 0; i < instruments.size(); ++i) {
    const auto& inst = instruments[i];
    const auto& replacement = replacementTracks[i];

    if (!inst.segments.empty()) {
      // Legacy explicit segments mode (timeline windows).
      for (const auto& seg : inst.segments) {
        if (seg.startFrame >= songFrames) continue;
        if (seg.startFrame >= replacement.frameCount) continue;

        uint64_t frameCount = seg.frameCount;
        const uint64_t maxToSong = songFrames - seg.startFrame;
        if (frameCount > maxToSong) frameCount = maxToSong;

        const uint64_t segEnd = seg.startFrame + frameCount;
        if (segEnd > replacement.frameCount) {
          const uint64_t clamped = replacement.frameCount - seg.startFrame;
          frameCount = std::min<uint64_t>(frameCount, clamped);
        }
        if (frameCount == 0) continue;

        crowdnoise_pcm_f32_t segView{};
        segView.sampleRate = 48000;
        segView.channels = 2;
        segView.frameCount = frameCount;
        segView.pcmInterleaved = replacement.pcmInterleaved + static_cast<size_t>(seg.startFrame * 2u);

        std::memset(err, 0, sizeof(err));
        const crowdnoise_result_t rc = crowdnoise_step5_add_track_to_mix(
            &segView, seg.startFrame, &mix, err, sizeof(err));
        if (rc != CROWDNOISE_OK) {
          std::cerr << "Step 5 add failed for instrument '" << inst.name << "': " << err << "\n";
          for (size_t j = 0; j < instruments.size(); ++j) {
            crowdnoise_free_pcm_f32(&replacementTracks[j]);
            if (hasOriginal[j]) crowdnoise_free_pcm_f32(&originalTracks[j]);
          }
          crowdnoise_free_pcm_f32(&mix);
          return 1;
        }
      }
      continue;
    }

    if (hasOriginal[i]) {
      // Mask-from-original mode: render a full-length stem with silence where the original is silent.
      renderedStems.emplace_back();
      RenderMaskedReplacementStem(originalTracks[i], replacement, songFrames, gateParams, &renderedStems.back());

      crowdnoise_pcm_f32_t stemView{};
      stemView.sampleRate = 48000;
      stemView.channels = 2;
      stemView.frameCount = songFrames;
      stemView.pcmInterleaved = renderedStems.back().data();

      std::memset(err, 0, sizeof(err));
      const crowdnoise_result_t rc = crowdnoise_step5_add_track_to_mix(&stemView, 0u, &mix, err, sizeof(err));
      if (rc != CROWDNOISE_OK) {
        std::cerr << "Step 5 add failed for instrument '" << inst.name << "': " << err << "\n";
        for (size_t j = 0; j < instruments.size(); ++j) {
          crowdnoise_free_pcm_f32(&replacementTracks[j]);
          if (hasOriginal[j]) crowdnoise_free_pcm_f32(&originalTracks[j]);
        }
        crowdnoise_free_pcm_f32(&mix);
        return 1;
      }
      continue;
    }

    // Full-stem mode: mix replacement from t=0, clamped to song length.
    if (replacement.frameCount == 0) continue;
    const uint64_t frameCount = std::min<uint64_t>(replacement.frameCount, songFrames);
    crowdnoise_pcm_f32_t view{};
    view.sampleRate = 48000;
    view.channels = 2;
    view.frameCount = frameCount;
    view.pcmInterleaved = replacement.pcmInterleaved;
    std::memset(err, 0, sizeof(err));
    const crowdnoise_result_t rc = crowdnoise_step5_add_track_to_mix(&view, 0u, &mix, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 5 add failed for instrument '" << inst.name << "': " << err << "\n";
      for (size_t j = 0; j < instruments.size(); ++j) {
        crowdnoise_free_pcm_f32(&replacementTracks[j]);
        if (hasOriginal[j]) crowdnoise_free_pcm_f32(&originalTracks[j]);
      }
      crowdnoise_free_pcm_f32(&mix);
      return 1;
    }
  }

  // 5) Step 6: prevent clipping distortion (constant scale).
  float maxAbs = 0.0f;
  float scaleApplied = 1.0f;
  std::memset(err, 0, sizeof(err));
  {
    const crowdnoise_result_t rc = crowdnoise_step6_prevent_clipping_in_place(
        &mix, &maxAbs, &scaleApplied, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 6 failed: " << err << "\n";
      for (size_t i = 0; i < instruments.size(); ++i) {
        crowdnoise_free_pcm_f32(&replacementTracks[i]);
        if (hasOriginal[i]) crowdnoise_free_pcm_f32(&originalTracks[i]);
      }
      crowdnoise_free_pcm_f32(&mix);
      return 1;
    }
  }

  // 6) Step 7: encode to MP3.
  std::memset(err, 0, sizeof(err));
  {
    const int vbrQuality = 4;  // good default
    const crowdnoise_result_t rc = crowdnoise_step7_encode_mix_to_mp3_file(
        &mix, outMp3Path.c_str(), vbrQuality, err, sizeof(err));
    if (rc != CROWDNOISE_OK) {
      std::cerr << "Step 7 encode failed: " << err << "\n";
      for (size_t i = 0; i < instruments.size(); ++i) {
        crowdnoise_free_pcm_f32(&replacementTracks[i]);
        if (hasOriginal[i]) crowdnoise_free_pcm_f32(&originalTracks[i]);
      }
      crowdnoise_free_pcm_f32(&mix);
      return 1;
    }
  }

  // Cleanup.
  for (size_t i = 0; i < instruments.size(); ++i) {
    crowdnoise_free_pcm_f32(&replacementTracks[i]);
    if (hasOriginal[i]) crowdnoise_free_pcm_f32(&originalTracks[i]);
  }
  crowdnoise_free_pcm_f32(&mix);

  std::cout << "Wrote: " << outMp3Path << "\n";
  std::cout << "Step 6 maxAbs=" << maxAbs << " scaleApplied=" << scaleApplied << "\n";
  return 0;
}

