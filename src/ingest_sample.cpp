// crowd·noise - sample ingest + canonicalize to standardized MP3
//
// Goals (v0):
// - Always preserve the original uploaded file bytes (audio OR video) for provenance.
// - Produce a single standardized "canonical" MP3 intended for later pitch/time-stretching.
// - No cleanup (no trimming, no denoise, no loudness normalization); preserve timing + perceived volume as much as possible.
// - If decode/transcode fails, keep the original anyway and return machine-readable output for the caller/DB.
//
// Dependencies:
// - ffmpeg (battle-tested decoder/encoder)
// - ffprobe (optional, for metadata; typically ships with ffmpeg)
//
// Build (WSL/Linux):
//   g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o ingest_sample ingest_sample.cpp
//
// Usage:
//   ./ingest_sample --input /path/to/clip.mov
//   ./ingest_sample --input /path/to/audio.m4a --storage-root ./storage
//
// Output:
// - Prints a JSON object to stdout with paths + status.
// - Exit code 0 on success (canonical mp3 created), nonzero otherwise.

#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string jsonEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::ostringstream oss;
          oss << "\\u"
              << std::hex << std::uppercase
              << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(c));
          out += oss.str();
        } else {
          out += c;
        }
    }
  }
  return out;
}

static std::string nowIso8601Utc() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return std::string(buf);
}

static std::string randomIdHex32() {
  // 128-bit random id, rendered as 32 hex chars.
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
  const uint64_t a = dist(gen);
  const uint64_t b = dist(gen);
  std::ostringstream oss;
  oss << std::hex;
  oss.width(16); oss.fill('0'); oss << a;
  oss.width(16); oss.fill('0'); oss << b;
  return oss.str();
}

struct ProcessResult {
  int exit_code = -1;
  bool exited_normally = false;
  int term_signal = 0;
  std::string spawn_error;
};

static ProcessResult runProcess(const std::vector<std::string>& args,
                                const std::optional<fs::path>& stdout_path,
                                const std::optional<fs::path>& stderr_path) {
  ProcessResult res;
  if (args.empty()) {
    res.spawn_error = "no argv provided";
    return res;
  }

  int out_fd = -1;
  int err_fd = -1;
  if (stdout_path) {
    out_fd = ::open(stdout_path->c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (out_fd < 0) {
      res.spawn_error = "failed to open stdout file: " + std::string(std::strerror(errno));
      return res;
    }
  }
  if (stderr_path) {
    err_fd = ::open(stderr_path->c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (err_fd < 0) {
      if (out_fd >= 0) ::close(out_fd);
      res.spawn_error = "failed to open stderr file: " + std::string(std::strerror(errno));
      return res;
    }
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    if (out_fd >= 0) ::close(out_fd);
    if (err_fd >= 0) ::close(err_fd);
    res.spawn_error = "fork failed: " + std::string(std::strerror(errno));
    return res;
  }

  if (pid == 0) {
    // Child
    if (out_fd >= 0) {
      (void)::dup2(out_fd, STDOUT_FILENO);
    }
    if (err_fd >= 0) {
      (void)::dup2(err_fd, STDERR_FILENO);
    }
    if (out_fd >= 0) ::close(out_fd);
    if (err_fd >= 0) ::close(err_fd);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    ::execvp(argv[0], argv.data());
    // If exec fails:
    std::cerr << "execvp failed for '" << args[0] << "': " << std::strerror(errno) << "\n";
    _exit(127);
  }

  // Parent
  if (out_fd >= 0) ::close(out_fd);
  if (err_fd >= 0) ::close(err_fd);

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    res.spawn_error = "waitpid failed: " + std::string(std::strerror(errno));
    return res;
  }

  if (WIFEXITED(status)) {
    res.exited_normally = true;
    res.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    res.exited_normally = false;
    res.term_signal = WTERMSIG(status);
    res.exit_code = 128 + res.term_signal;
  }
  return res;
}

struct ProbeInfo {
  std::optional<double> duration_seconds;
  std::optional<int> bit_rate_bps;
  std::optional<int> sample_rate_hz;
  std::optional<int> channels;
};

static std::optional<std::string> readWholeFile(const fs::path& p) {
  std::ifstream in(p, std::ios::in | std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static std::optional<double> parseDoubleLine(const std::string& s) {
  try {
    size_t idx = 0;
    const double v = std::stod(s, &idx);
    (void)idx;
    return v;
  } catch (...) {
    return std::nullopt;
  }
}

static std::optional<int> parseIntLine(const std::string& s) {
  try {
    size_t idx = 0;
    const int v = std::stoi(s, &idx);
    (void)idx;
    return v;
  } catch (...) {
    return std::nullopt;
  }
}

static ProbeInfo probeCanonical(const std::string& ffprobe_bin,
                                const fs::path& canonical_mp3,
                                const fs::path& tmp_dir) {
  ProbeInfo info;

  // format: duration, bit_rate
  const fs::path fmt_out = tmp_dir / "ffprobe_format.txt";
  const fs::path fmt_err = tmp_dir / "ffprobe_format.err.txt";
  {
    std::vector<std::string> args = {
        ffprobe_bin,
        "-v", "error",
        "-show_entries", "format=duration,bit_rate",
        "-of", "default=noprint_wrappers=1:nokey=1",
        canonical_mp3.string(),
    };
    const auto r = runProcess(args, fmt_out, fmt_err);
    if (r.exited_normally && r.exit_code == 0) {
      const auto content = readWholeFile(fmt_out);
      if (content) {
        std::istringstream iss(*content);
        std::string line1, line2;
        std::getline(iss, line1);
        std::getline(iss, line2);
        if (!line1.empty()) info.duration_seconds = parseDoubleLine(line1);
        if (!line2.empty()) info.bit_rate_bps = parseIntLine(line2);
      }
    }
  }

  // stream: sample_rate, channels (audio stream 0)
  const fs::path st_out = tmp_dir / "ffprobe_stream.txt";
  const fs::path st_err = tmp_dir / "ffprobe_stream.err.txt";
  {
    std::vector<std::string> args = {
        ffprobe_bin,
        "-v", "error",
        "-select_streams", "a:0",
        "-show_entries", "stream=sample_rate,channels",
        "-of", "default=noprint_wrappers=1:nokey=1",
        canonical_mp3.string(),
    };
    const auto r = runProcess(args, st_out, st_err);
    if (r.exited_normally && r.exit_code == 0) {
      const auto content = readWholeFile(st_out);
      if (content) {
        std::istringstream iss(*content);
        std::string line1, line2;
        std::getline(iss, line1);
        std::getline(iss, line2);
        if (!line1.empty()) info.sample_rate_hz = parseIntLine(line1);
        if (!line2.empty()) info.channels = parseIntLine(line2);
      }
    }
  }

  return info;
}

struct Options {
  fs::path input;
  fs::path storage_root = fs::path("storage");
  std::string sample_id;
  std::string ffmpeg_bin = "ffmpeg";
  std::string ffprobe_bin = "ffprobe";

  // Canonicalization defaults (picked to be simple + robust for later pitch/time stretching):
  int sample_rate_hz = 48000;
  int channels = 1;  // 1=mono (default), 2=stereo
  int lame_vbr_q = 2;  // 0(best)-9(worst)
};

static void printUsage(const char* argv0) {
  std::cerr
      << "Usage:\n"
      << "  " << argv0 << " --input <path> [--storage-root <dir>] [--sample-id <id>]\n"
      << "         [--ffmpeg <bin>] [--ffprobe <bin>] [--sample-rate <hz>] [--channels <1|2>]\n"
      << "         [--vbr-quality <0-9>]\n";
}

static std::optional<Options> parseArgs(int argc, char** argv, std::string* err) {
  Options opt;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto need = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) {
        if (err) *err = std::string("missing value for ") + flag;
        return {};
      }
      return argv[++i];
    };

    if (a == "--input") {
      opt.input = need("--input");
      if (opt.input.empty()) return std::nullopt;
    } else if (a == "--storage-root") {
      opt.storage_root = need("--storage-root");
    } else if (a == "--sample-id") {
      opt.sample_id = need("--sample-id");
    } else if (a == "--ffmpeg") {
      opt.ffmpeg_bin = need("--ffmpeg");
    } else if (a == "--ffprobe") {
      opt.ffprobe_bin = need("--ffprobe");
    } else if (a == "--sample-rate") {
      const auto v = need("--sample-rate");
      opt.sample_rate_hz = std::stoi(v);
    } else if (a == "--channels") {
      const auto v = need("--channels");
      opt.channels = std::stoi(v);
      if (!(opt.channels == 1 || opt.channels == 2)) {
        if (err) *err = "--channels must be 1 or 2";
        return std::nullopt;
      }
    } else if (a == "--vbr-quality") {
      const auto v = need("--vbr-quality");
      opt.lame_vbr_q = std::stoi(v);
      if (opt.lame_vbr_q < 0 || opt.lame_vbr_q > 9) {
        if (err) *err = "--vbr-quality must be between 0 and 9";
        return std::nullopt;
      }
    } else if (a == "--help" || a == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    } else {
      if (err) *err = "unknown arg: " + a;
      return std::nullopt;
    }
  }

  if (opt.input.empty()) {
    if (err) *err = "missing --input";
    return std::nullopt;
  }
  if (opt.sample_id.empty()) opt.sample_id = randomIdHex32();
  return opt;
}

static std::optional<uintmax_t> fileSize(const fs::path& p) {
  std::error_code ec;
  const auto s = fs::file_size(p, ec);
  if (ec) return std::nullopt;
  return s;
}

int main(int argc, char** argv) {
  std::string parse_err;
  const auto optMaybe = parseArgs(argc, argv, &parse_err);
  if (!optMaybe) {
    std::cerr << "Argument error: " << parse_err << "\n";
    printUsage(argv[0]);
    return 10;
  }
  const Options opt = *optMaybe;

  // Resolve + validate input early.
  std::error_code ec;
  const fs::path input_abs = fs::absolute(opt.input, ec);
  const fs::path input_path = ec ? opt.input : input_abs;

  const bool input_exists = fs::exists(input_path, ec) && !ec;
  const auto input_bytes = input_exists ? fileSize(input_path) : std::nullopt;

  const std::string created_at = nowIso8601Utc();

  // Storage layout:
  // storage_root/
  //   samples/<sample_id>/
  //     original/original.<ext>
  //     canonical/canonical.mp3
  //     logs/ffmpeg.log
  const fs::path sample_dir = opt.storage_root / "samples" / opt.sample_id;
  const fs::path original_dir = sample_dir / "original";
  const fs::path canonical_dir = sample_dir / "canonical";
  const fs::path logs_dir = sample_dir / "logs";

  fs::create_directories(original_dir, ec);
  if (ec) {
    std::cout << "{"
              << "\"status\":\"error\","
              << "\"error\":\"" << jsonEscape("failed to create original dir: " + ec.message()) << "\","
              << "\"sample_id\":\"" << jsonEscape(opt.sample_id) << "\""
              << "}\n";
    return 30;
  }
  fs::create_directories(canonical_dir, ec);
  fs::create_directories(logs_dir, ec);

  fs::path stored_original_path;
  bool original_saved = false;

  if (input_exists) {
    std::string ext = input_path.extension().string();
    if (ext.empty()) ext = ".bin";
    stored_original_path = original_dir / ("original" + ext);

    std::error_code copy_ec;
    fs::copy_file(input_path, stored_original_path, fs::copy_options::overwrite_existing, copy_ec);
    if (!copy_ec) original_saved = true;
  }

  // Build the canonical mp3 (best-effort). Even if this fails, we keep the original.
  const fs::path canonical_mp3 = canonical_dir / "canonical.mp3";
  const fs::path canonical_tmp = canonical_dir / "canonical.tmp.mp3";
  const fs::path ffmpeg_log = logs_dir / "ffmpeg.log";

  bool canonical_ok = false;
  ProcessResult ffmpeg_res;
  std::string canonical_error;

  if (!input_exists) {
    canonical_error = "input file does not exist";
  } else {
    // ffmpeg args:
    // -i <input>        (audio or video)
    // -vn               (ignore video)
    // -ac <n>           (standardize channel layout)
    // -ar <hz>          (standardize sample rate)
    // -c:a libmp3lame   (battle-tested mp3 encoder)
    // -q:a <0-9>        (VBR quality, no loudness normalization)
    // -map_metadata -1  (do not carry input tags into canonical copy; provenance lives in DB)
    std::vector<std::string> args = {
        opt.ffmpeg_bin,
        "-hide_banner",
        "-y",
        "-i", input_path.string(),
        "-vn",
        "-ac", std::to_string(opt.channels),
        "-ar", std::to_string(opt.sample_rate_hz),
        "-c:a", "libmp3lame",
        "-q:a", std::to_string(opt.lame_vbr_q),
        "-map_metadata", "-1",
        canonical_tmp.string(),
    };

    // ffmpeg prints most logs to stderr; capture both streams to the same file for debugging.
    ffmpeg_res = runProcess(args, ffmpeg_log, ffmpeg_log);

    if (ffmpeg_res.exited_normally && ffmpeg_res.exit_code == 0) {
      std::error_code rename_ec;
      fs::rename(canonical_tmp, canonical_mp3, rename_ec);
      if (rename_ec) {
        canonical_error = "ffmpeg succeeded but rename failed: " + rename_ec.message();
      } else {
        canonical_ok = true;
      }
    } else {
      canonical_error = "ffmpeg failed";
      // Clean up temp output if it exists.
      std::error_code rm_ec;
      if (fs::exists(canonical_tmp, rm_ec)) fs::remove(canonical_tmp, rm_ec);
    }
  }

  // Optionally probe canonical mp3 for metadata if it exists.
  ProbeInfo probe{};
  if (canonical_ok) {
    std::error_code d_ec;
    const fs::path probe_tmp = logs_dir / "probe";
    fs::create_directories(probe_tmp, d_ec);
    if (!d_ec) {
      probe = probeCanonical(opt.ffprobe_bin, canonical_mp3, probe_tmp);
    }
  }

  // Emit machine-readable JSON for backend/DB insertion.
  // Note: we do not embed provenance into the MP3; the caller stores provenance in its DB.
  std::ostringstream out;
  out << "{";
  out << "\"sample_id\":\"" << jsonEscape(opt.sample_id) << "\",";
  out << "\"created_at\":\"" << jsonEscape(created_at) << "\",";
  out << "\"input_path\":\"" << jsonEscape(input_path.string()) << "\",";
  out << "\"input_exists\":" << (input_exists ? "true" : "false") << ",";
  if (input_bytes) out << "\"input_bytes\":" << *input_bytes << ",";

  out << "\"stored_original\":{";
  out << "\"saved\":" << (original_saved ? "true" : "false");
  if (original_saved) {
    out << ",\"path\":\"" << jsonEscape(stored_original_path.string()) << "\"";
  }
  out << "},";

  out << "\"canonical_mp3\":{";
  out << "\"created\":" << (canonical_ok ? "true" : "false") << ",";
  out << "\"path\":\"" << jsonEscape(canonical_mp3.string()) << "\",";
  out << "\"standard\":{";
  out << "\"codec\":\"mp3\",";
  out << "\"encoder\":\"libmp3lame\",";
  out << "\"sample_rate_hz\":" << opt.sample_rate_hz << ",";
  out << "\"channels\":" << opt.channels << ",";
  out << "\"loudness\":\"none\",";
  out << "\"trim\":\"none\"";
  out << "}";

  if (canonical_ok) {
    if (const auto s = fileSize(canonical_mp3)) out << ",\"bytes\":" << *s;
    if (probe.duration_seconds) out << ",\"duration_seconds\":" << *probe.duration_seconds;
    if (probe.bit_rate_bps) out << ",\"bit_rate_bps\":" << *probe.bit_rate_bps;
    if (probe.sample_rate_hz) out << ",\"detected_sample_rate_hz\":" << *probe.sample_rate_hz;
    if (probe.channels) out << ",\"detected_channels\":" << *probe.channels;
  } else {
    out << ",\"error\":\"" << jsonEscape(canonical_error) << "\"";
    if (!ffmpeg_res.spawn_error.empty()) {
      out << ",\"spawn_error\":\"" << jsonEscape(ffmpeg_res.spawn_error) << "\"";
    }
    if (ffmpeg_res.exit_code != -1) out << ",\"ffmpeg_exit_code\":" << ffmpeg_res.exit_code;
    if (ffmpeg_res.term_signal != 0) out << ",\"ffmpeg_term_signal\":" << ffmpeg_res.term_signal;
  }
  out << "},";

  out << "\"logs\":{";
  out << "\"ffmpeg_log\":\"" << jsonEscape(ffmpeg_log.string()) << "\"";
  out << "},";

  out << "\"status\":\"" << (canonical_ok ? "ok" : "needs_rerecord_or_reprocess") << "\"";
  out << "}\n";

  std::cout << out.str();

  if (canonical_ok) return 0;
  // Distinguish "we still stored the original" vs "we couldn't even store original".
  if (original_saved) return 2;
  return 3;
}

