#include "crowdnoise_native_core.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

static void PrintUsage(const char* exe) {
  std::cerr
      << "Usage:\n"
      << "  " << exe << " --times TIMES.csv --sample HIT.mp3 --out OUT.mp3 [--length-seconds S] [--gain G] [--vbr Q]\n"
      << "\n"
      << "CSV format:\n"
      << "  index,time_seconds\n"
      << "  0,0.023220\n"
      << "  1,0.116100\n"
      << "\n"
      << "Notes:\n"
      << "  - `time_seconds` is when each hit starts.\n"
      << "  - Output length defaults to (last_time + sample_duration).\n"
      << "  - gain is a linear multiplier applied to the decoded hit PCM (default 1.0).\n"
      << "  - vbr is LAME VBR quality 0..9 (default 4).\n";
}

static std::string Trim(std::string s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  s = s.substr(a, b - a);
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

static std::vector<std::string> SplitCsvLine(const std::string& line) {
  // Minimal CSV splitter: good enough for the repo's simple numeric CSVs.
  std::vector<std::string> out;
  std::string cur;
  for (char c : line) {
    if (c == ',') {
      out.push_back(Trim(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(Trim(cur));
  return out;
}

static bool ParseDoubleStrict(const std::string& s, double* out) {
  if (!out) return false;
  const std::string t = Trim(s);
  if (t.empty()) return false;
  char* endp = nullptr;
  const double v = std::strtod(t.c_str(), &endp);
  if (!endp || *endp != '\0') return false;
  if (!std::isfinite(v)) return false;
  *out = v;
  return true;
}

static bool LoadTimesCsv(const std::string& path, std::vector<double>* outTimes, std::string* err) {
  if (!outTimes) return false;
  outTimes->clear();

  std::ifstream f(path);
  if (!f.is_open()) {
    if (err) *err = "Failed to open CSV: " + path;
    return false;
  }

  std::string header;
  if (!std::getline(f, header)) {
    if (err) *err = "Empty CSV: " + path;
    return false;
  }
  const auto cols = SplitCsvLine(header);
  int timeCol = -1;
  for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
    if (cols[static_cast<size_t>(i)] == "time_seconds") {
      timeCol = i;
      break;
    }
  }
  if (timeCol < 0) {
    if (err) *err = "CSV missing column 'time_seconds' (expected: index,time_seconds): " + path;
    return false;
  }

  std::string line;
  int row = 1;
  while (std::getline(f, line)) {
    row++;
    line = Trim(line);
    if (line.empty()) continue;
    const auto fields = SplitCsvLine(line);
    if (timeCol >= static_cast<int>(fields.size())) continue;

    double t = 0.0;
    if (!ParseDoubleStrict(fields[static_cast<size_t>(timeCol)], &t)) {
      if (err) {
        *err = "Bad time_seconds at row " + std::to_string(row) + ": '" + fields[static_cast<size_t>(timeCol)] + "'";
      }
      return false;
    }
    if (t < 0.0) continue;
    outTimes->push_back(t);
  }

  if (outTimes->empty()) {
    if (err) *err = "No timestamps found in CSV: " + path;
    return false;
  }

  std::sort(outTimes->begin(), outTimes->end());
  return true;
}

static std::string GetArg(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == key && i + 1 < argc) return std::string(argv[i + 1]);
  }
  return std::string();
}

int main(int argc, char** argv) {
  const std::string timesCsv = GetArg(argc, argv, "--times");
  const std::string sampleMp3 = GetArg(argc, argv, "--sample");
  const std::string outMp3 = GetArg(argc, argv, "--out");

  if (timesCsv.empty() || sampleMp3.empty() || outMp3.empty()) {
    PrintUsage(argv[0]);
    return 2;
  }

  double lengthSecondsOverride = -1.0;
  if (const std::string s = GetArg(argc, argv, "--length-seconds"); !s.empty()) {
    if (!ParseDoubleStrict(s, &lengthSecondsOverride) || !(lengthSecondsOverride > 0.0)) {
      std::cerr << "Bad --length-seconds: " << s << "\n";
      return 2;
    }
  }

  double gain = 1.0;
  if (const std::string s = GetArg(argc, argv, "--gain"); !s.empty()) {
    if (!ParseDoubleStrict(s, &gain) || !std::isfinite(gain) || gain < 0.0) {
      std::cerr << "Bad --gain: " << s << "\n";
      return 2;
    }
  }

  int vbr = 4;
  if (const std::string s = GetArg(argc, argv, "--vbr"); !s.empty()) {
    try {
      vbr = std::stoi(Trim(s));
    } catch (...) {
      std::cerr << "Bad --vbr: " << s << "\n";
      return 2;
    }
    if (vbr < 0 || vbr > 9) {
      std::cerr << "--vbr must be 0..9\n";
      return 2;
    }
  }

  std::vector<double> times;
  std::string csvErr;
  if (!LoadTimesCsv(timesCsv, &times, &csvErr)) {
    std::cerr << csvErr << "\n";
    return 1;
  }

  char err[512];
  std::memset(err, 0, sizeof(err));

  crowdnoise_pcm_f32_t hit{};
  const crowdnoise_result_t rc = crowdnoise_decode_standardize_mp3_file(sampleMp3.c_str(), &hit, err, sizeof(err));
  if (rc != CROWDNOISE_OK) {
    std::cerr << "Decode+standardize failed for sample (" << sampleMp3 << "): " << err << "\n";
    return 1;
  }

  // Apply gain in place (linear).
  if (gain != 1.0 && hit.pcmInterleaved && hit.frameCount > 0 && hit.channels == 2) {
    const size_t samples = static_cast<size_t>(hit.frameCount * 2u);
    for (size_t i = 0; i < samples; ++i) {
      hit.pcmInterleaved[i] *= static_cast<float>(gain);
    }
  }

  // Choose output length.
  uint64_t outFrames = 0;
  if (lengthSecondsOverride > 0.0) {
    outFrames = crowdnoise_seconds_to_sample_index(lengthSecondsOverride, 48000);
  } else {
    const double lastTime = times.back();
    const double sampleSeconds = (hit.sampleRate > 0) ? (static_cast<double>(hit.frameCount) / 48000.0) : 0.0;
    const double totalSeconds = std::max(0.0, lastTime) + std::max(0.0, sampleSeconds);
    outFrames = crowdnoise_seconds_to_sample_index(totalSeconds, 48000);
  }
  if (outFrames == 0) outFrames = hit.frameCount;
  if (outFrames == 0) {
    std::cerr << "Sample has 0 frames.\n";
    crowdnoise_free_pcm_f32(&hit);
    return 1;
  }

  crowdnoise_track_placement_t placement{0u, outFrames};
  crowdnoise_pcm_f32_t mix{};
  std::memset(err, 0, sizeof(err));
  const crowdnoise_result_t rc4 = crowdnoise_step4_allocate_silent_mix_buffer(&placement, 1, &mix, err, sizeof(err));
  if (rc4 != CROWDNOISE_OK) {
    std::cerr << "Step 4 allocate failed: " << err << "\n";
    crowdnoise_free_pcm_f32(&hit);
    return 1;
  }

  // Mix hit at each timestamp.
  for (double t : times) {
    if (!(t >= 0.0)) continue;
    const uint64_t startFrame = crowdnoise_seconds_to_sample_index(t, 48000);
    if (startFrame >= mix.frameCount) continue;

    uint64_t framesToMix = hit.frameCount;
    const uint64_t maxToEnd = mix.frameCount - startFrame;
    if (framesToMix > maxToEnd) framesToMix = maxToEnd;
    if (framesToMix == 0) continue;

    crowdnoise_pcm_f32_t view = hit;
    view.frameCount = framesToMix;

    std::memset(err, 0, sizeof(err));
    const crowdnoise_result_t rc5 = crowdnoise_step5_add_track_to_mix(&view, startFrame, &mix, err, sizeof(err));
    if (rc5 != CROWDNOISE_OK) {
      std::cerr << "Step 5 add failed at t=" << t << "s (frame " << startFrame << "): " << err << "\n";
      crowdnoise_free_pcm_f32(&hit);
      crowdnoise_free_pcm_f32(&mix);
      return 1;
    }
  }

  float maxAbs = 0.0f;
  float scaleApplied = 1.0f;
  std::memset(err, 0, sizeof(err));
  const crowdnoise_result_t rc6 = crowdnoise_step6_prevent_clipping_in_place(&mix, &maxAbs, &scaleApplied, err, sizeof(err));
  if (rc6 != CROWDNOISE_OK) {
    std::cerr << "Step 6 failed: " << err << "\n";
    crowdnoise_free_pcm_f32(&hit);
    crowdnoise_free_pcm_f32(&mix);
    return 1;
  }

  std::memset(err, 0, sizeof(err));
  const crowdnoise_result_t rc7 = crowdnoise_step7_encode_mix_to_mp3_file(&mix, outMp3.c_str(), vbr, err, sizeof(err));
  if (rc7 != CROWDNOISE_OK) {
    std::cerr << "Step 7 encode failed: " << err << "\n";
    crowdnoise_free_pcm_f32(&hit);
    crowdnoise_free_pcm_f32(&mix);
    return 1;
  }

  std::cout << "Wrote: " << outMp3 << "\n"
            << "Hits: " << times.size() << "\n"
            << "Step 6 maxAbs=" << maxAbs << " scaleApplied=" << scaleApplied << "\n";

  crowdnoise_free_pcm_f32(&hit);
  crowdnoise_free_pcm_f32(&mix);
  return 0;
}

