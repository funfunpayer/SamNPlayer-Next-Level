#pragma once

// Golden-clip finder (samn_goldenclip): where in a long video is the most
// movement, and which stretch of it makes (1) a clip of about three minutes
// and (2) a small clip of dense movement under a size cap. Header-only, so
// the engine tests pin the choice of windows without running the tool.
// ffmpeg does the cutting, as an external program; nothing here links it.
//
// The clips and the manifest stay on the user's disk. No media enters any
// repository; the manifest carries numbers and file names only.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>

#include "samn/clip_prep.h"
#include "samn/fileio.h"
#include "samn/funscript.h"
#include "samn/vision/frame_reader.h"

namespace samn::golden {

// ---------------------------------------------------------------- measuring

// Movement per second of video: the median, over the samples of that second,
// of the mean absolute difference between two small grey pictures taken
// 1 / sample_fps apart (0-255 grey levels). The median keeps one hard cut
// from making a second look busy.
struct MotionSeries {
    std::vector<double> motion;  // one value per whole second; 0 where nothing was measured
    double duration_s = 0;
    double fps = 0;
    int width = 0, height = 0;
};

struct MotionOptions {
    double sample_fps = 4;   // pictures compared per second
    int width = 160;         // they are scaled to this width first
    // 0: every second of the video is measured (the whole file is decoded).
    // N > 1: one second is measured every N seconds, by seeking, and stands
    // for its block: much faster on a long 4K file, coarser.
    int probe_every_s = 0;
    std::function<void(double done_share)> progress;
    std::function<bool()> cancel;
};

inline double MeanAbsDiff(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat d;
    cv::absdiff(a, b, d);
    return cv::mean(d)[0];
}

inline std::optional<MotionSeries> MeasureMotion(const std::filesystem::path& video, const MotionOptions& o,
                                                 std::string* error) {
    auto reader = samn::vision::FrameReader::Open(video, error);
    if (!reader) return std::nullopt;
    MotionSeries s;
    s.fps = reader->fps();
    s.width = reader->size().width;
    s.height = reader->size().height;
    if (s.width <= 0 || s.height <= 0) {
        if (error) *error = "the video has no picture size";
        return std::nullopt;
    }
    const double nominal_s = s.fps > 0 ? reader->frameCount() / s.fps : 0;
    const int small_w = std::max(16, std::min(o.width, s.width));
    const int small_h = std::max(8, int(std::lround(double(s.height) * small_w / s.width)));
    const double gap_us = 1e6 / std::max(0.5, o.sample_fps);
    std::vector<std::vector<double>> bins;
    auto add = [&](double t_s, double v) {
        const size_t b = size_t(std::max(0.0, std::floor(t_s)));
        if (bins.size() <= b) bins.resize(b + 1);
        bins[b].push_back(v);
    };
    samn::vision::VideoFrame frame;
    cv::Mat small, gray, prev;
    int64_t prev_us = -1, last_us = 0;
    auto take = [&](int64_t pts_us) {
        if (!reader->Retrieve(frame)) return;
        cv::resize(frame.bgr, small, {small_w, small_h}, 0, 0, cv::INTER_AREA);
        cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
        // Two samples further apart than twice the step are not compared (a seek lies between).
        if (!prev.empty() && pts_us - prev_us <= 2.5 * gap_us) add(pts_us / 1e6, MeanAbsDiff(gray, prev));
        gray.copyTo(prev);
        prev_us = pts_us;
    };
    if (o.probe_every_s > 1 && nominal_s > 0) {
        for (double block = 0; block < nominal_s; block += o.probe_every_s) {
            if (o.cancel && o.cancel()) break;
            if (!reader->SeekMs(int64_t(block * 1000))) break;
            prev.release();
            prev_us = -1;
            int64_t pts = 0, next = -1;
            while (reader->Grab(&pts)) {
                last_us = std::max(last_us, pts);
                if (pts > (block + 1.3) * 1e6) break;
                if (next < 0 || pts >= next) {
                    take(pts);
                    next = pts + int64_t(gap_us) - 1000;
                }
            }
            if (o.progress) o.progress(std::min(1.0, block / nominal_s));
        }
        // Each measured second stands for its block.
        std::vector<std::vector<double>> filled(size_t(std::ceil(nominal_s)));
        for (size_t b = 0; b < filled.size(); ++b) {
            const size_t src = (b / size_t(o.probe_every_s)) * size_t(o.probe_every_s);
            for (size_t k = src; k < std::min(bins.size(), src + 2); ++k)
                filled[b].insert(filled[b].end(), bins[k].begin(), bins[k].end());
        }
        bins = std::move(filled);
        s.duration_s = nominal_s;
    } else {
        int64_t pts = 0, next = -1, n = 0;
        while (reader->Grab(&pts)) {
            if (o.cancel && (n & 63) == 0 && o.cancel()) break;
            last_us = std::max(last_us, pts);
            if (next < 0 || pts >= next) {
                take(pts);
                next = pts + int64_t(gap_us) - 1000;
            }
            if (o.progress && nominal_s > 0 && (++n % 500) == 0) o.progress(std::min(1.0, pts / 1e6 / nominal_s));
        }
        s.duration_s = std::max(nominal_s, last_us / 1e6 + (s.fps > 0 ? 1 / s.fps : 0));
    }
    s.motion.assign(size_t(std::max(1.0, std::ceil(s.duration_s))), 0.0);
    for (size_t b = 0; b < bins.size() && b < s.motion.size(); ++b) {
        if (bins[b].empty()) continue;
        std::sort(bins[b].begin(), bins[b].end());
        s.motion[b] = bins[b][bins[b].size() / 2];
    }
    if (o.progress) o.progress(1.0);
    return s;
}

// Stroke travel of a script per second of video: the sum of |position change|
// (0-100 units) that falls into each whole second.
inline std::vector<double> ScriptActivity(const std::vector<samn::Action>& actions, size_t seconds) {
    std::vector<double> out(seconds, 0.0);
    for (size_t i = 1; i < actions.size(); ++i) {
        const double t0 = actions[i - 1].at / 1000.0, t1 = actions[i].at / 1000.0;
        const double travel = std::abs(actions[i].pos - actions[i - 1].pos);
        if (t1 <= t0 || travel <= 0) continue;
        for (size_t b = size_t(std::max(0.0, std::floor(t0))); b < seconds && double(b) < t1; ++b) {
            const double overlap = std::min(t1, double(b + 1)) - std::max(t0, double(b));
            if (overlap > 0) out[b] += travel * overlap / (t1 - t0);
        }
    }
    return out;
}

// ---------------------------------------------------------------- choosing

inline double PercentileOf(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double pos = p / 100.0 * (v.size() - 1);
    const size_t lo = size_t(std::floor(pos)), hi = size_t(std::ceil(pos));
    return v[lo] + (v[hi] - v[lo]) * (pos - lo);
}

// Per second, 0..1: the movement against its own 95th percentile, and when a
// script is given the mean of that and the script's travel against its own
// 95th percentile. A second counts fully once it reaches the percentile, so
// one wild stretch does not decide alone.
inline std::vector<double> SecondScores(const std::vector<double>& motion, const std::vector<double>& activity) {
    auto norm = [](const std::vector<double>& v) {
        std::vector<double> out(v.size(), 0.0);
        const double top = PercentileOf(v, 95);
        if (top <= 1e-9) return out;
        for (size_t i = 0; i < v.size(); ++i) out[i] = std::min(1.0, v[i] / top);
        return out;
    };
    std::vector<double> m = norm(motion);
    if (activity.empty()) return m;
    const std::vector<double> a = norm(activity);
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5 * (m[i] + (i < a.size() ? a[i] : 0.0));
    return m;
}

struct Window {
    double start_s = 0, duration_s = 0;
    double score = 0;     // mean of SecondScores over the window, 0..1
    double motion = 0;    // mean movement value over the window (grey levels)
    double activity = 0;  // mean script travel per second over the window (0 without a script)
};

inline double MeanOf(const std::vector<double>& v, size_t from, size_t n) {
    double sum = 0;
    size_t count = 0;
    for (size_t i = from; i < from + n && i < v.size(); ++i) sum += v[i], ++count;
    return count ? sum / count : 0;
}

// The window of `seconds` whole seconds with the highest mean score; the
// earliest one on a tie. A video shorter than that gives the whole video.
inline Window BestWindow(const std::vector<double>& scores, const std::vector<double>& motion,
                         const std::vector<double>& activity, int seconds) {
    Window w;
    const size_t n = scores.size();
    const size_t len = size_t(std::max(1, std::min<int>(seconds, int(n))));
    if (n == 0) return w;
    double sum = 0;
    for (size_t i = 0; i < len; ++i) sum += scores[i];
    double best = sum;
    size_t best_at = 0;
    for (size_t i = len; i < n; ++i) {
        sum += scores[i] - scores[i - len];
        if (sum > best + 1e-12) best = sum, best_at = i - len + 1;
    }
    w.start_s = double(best_at);
    w.duration_s = double(len);
    w.score = best / len;
    w.motion = MeanOf(motion, best_at, len);
    w.activity = MeanOf(activity, best_at, len);
    return w;
}

struct PlanOptions {
    int long_s = 180;         // the long clip
    double max_mb = 25;       // the dense clip's size cap
    int dense_s = 60;         // its length when it is re-encoded, and the most a stream copy takes
    int min_dense_s = 20;     // a stream copy that fits less than this is re-encoded instead
    double min_motion = 0.5;  // grey levels: below this the densest stretch is not worth a clip
    int audio_kbps = 96;      // of a re-encoded dense clip
    int reencode_max_width = 1280;
};

struct Plan {
    Window long_clip;
    bool long_is_whole = false;  // the video is no longer than the long clip
    bool dense = false;          // a dense clip is worth cutting
    std::string dense_reason;    // why, or why not
    Window dense_clip;
    bool dense_reencode = false;
    int dense_video_kbps = 0;    // when re-encoded
};

// file_bytes / duration is the bit rate a stream copy keeps.
inline Plan PlanClips(const MotionSeries& series, const std::vector<double>& activity, std::uintmax_t file_bytes,
                      const PlanOptions& o) {
    Plan p;
    const std::vector<double> scores = SecondScores(series.motion, activity);
    p.long_clip = BestWindow(scores, series.motion, activity, o.long_s);
    p.long_is_whole = int(series.motion.size()) <= o.long_s;
    const double cap_bytes = o.max_mb * 1024 * 1024;
    const double bytes_per_s = series.duration_s > 0 ? double(file_bytes) / series.duration_s : 0;
    if (bytes_per_s > 0 && bytes_per_s * p.long_clip.duration_s <= cap_bytes) {
        p.dense_reason = "the long clip already fits the size cap";
        return p;
    }
    // A stream copy holds this many seconds under the cap (5 % kept for the container).
    const double copy_s = bytes_per_s > 0 ? 0.95 * cap_bytes / bytes_per_s : 0;
    int seconds = o.dense_s;
    if (copy_s >= o.min_dense_s) seconds = int(std::min<double>(o.dense_s, std::floor(copy_s)));
    else p.dense_reencode = true;
    p.dense_clip = BestWindow(scores, series.motion, activity, seconds);
    if (p.dense_clip.motion < o.min_motion) {
        p.dense_reason = "too little movement in the densest stretch";
        p.dense_reencode = false;
        return p;
    }
    p.dense = true;
    if (p.dense_reencode) {
        const double total_kbps = 0.92 * cap_bytes * 8 / 1000.0 / p.dense_clip.duration_s;
        p.dense_video_kbps = std::max(150, int(total_kbps) - o.audio_kbps);
        p.dense_reason = "re-encoded: a stream copy under the cap would be shorter than " +
                         std::to_string(o.min_dense_s) + " s";
    } else {
        p.dense_reason = "stream copy";
    }
    return p;
}

// ---------------------------------------------------------------- cutting

struct Tools {
    std::string ffmpeg = "ffmpeg";
    std::string ffprobe = "ffprobe";
};

inline std::string Quoted(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (const char ch : s) {
        if (ch == '\'') out += "'\\''";
        else out += ch;
    }
    return out + "'";
#endif
}

// Runs a command line without showing its output. True on exit code 0.
inline bool RunQuiet(const std::string& command, const std::filesystem::path& stdout_to = {}) {
#ifdef _WIN32
    // cmd.exe drops the outer pair of quotes when the line starts with one.
    const std::string out = stdout_to.empty() ? std::string("nul") : Quoted(stdout_to.u8string());
    const std::string line = "\"" + command + " >" + out + " 2>nul\"";
    const std::wstring wide = std::filesystem::u8path(line).wstring();
    return _wsystem(wide.c_str()) == 0;
#else
    const std::string out = stdout_to.empty() ? std::string("/dev/null") : Quoted(stdout_to.u8string());
    return std::system((command + " >" + out + " 2>/dev/null").c_str()) == 0;
#endif
}

inline bool HaveProgram(const std::string& program) { return RunQuiet(Quoted(program) + " -version"); }

// The time of the last key frame at or before t_s, read with ffprobe; nullopt
// when ffprobe is missing or reports none. A stream copy can only start on a
// key frame, so the cut is moved there and the script with it.
inline std::optional<double> KeyFrameAtOrBefore(const Tools& tools, const std::filesystem::path& video, double t_s,
                                                const std::filesystem::path& scratch) {
    for (const double back : {20.0, 120.0, 600.0}) {
        const double from = std::max(0.0, t_s - back);
        char interval[64];
        std::snprintf(interval, sizeof interval, "%.3f%%%.3f", from, t_s + 0.05);
        const std::string cmd = Quoted(tools.ffprobe) + " -v error -select_streams v:0 -skip_frame nokey" +
                                " -show_entries frame=pts_time -of csv=p=0 -read_intervals " + interval + " " +
                                Quoted(video.u8string());
        if (!RunQuiet(cmd, scratch)) return std::nullopt;
        std::ifstream in(scratch);
        std::string line;
        std::optional<double> best;
        while (std::getline(in, line)) {
            char* end = nullptr;
            const double v = std::strtod(line.c_str(), &end);
            if (end == line.c_str()) continue;
            if (v <= t_s + 1e-6 && (!best || v > *best)) best = v;
        }
        in.close();
        std::error_code ec;
        std::filesystem::remove(scratch, ec);
        if (best) return best;
        if (from <= 0) break;
    }
    return std::nullopt;
}

struct CutResult {
    bool ok = false;
    std::string mode;  // "copy" or "reencode"
    double start_s = 0, duration_s = 0;
    std::uintmax_t bytes = 0;
    std::string error;
};

inline std::string Seconds(double s) {
    char b[48];
    std::snprintf(b, sizeof b, "%.3f", s);
    return b;
}

// Stream copy from the key frame at or before start_s. The result's start_s
// is where the clip really begins in the source.
inline CutResult CutCopy(const Tools& tools, const std::filesystem::path& in, const std::filesystem::path& out,
                         double start_s, double duration_s) {
    CutResult r;
    r.mode = "copy";
    const auto key = KeyFrameAtOrBefore(tools, in, start_s, std::filesystem::path(out).replace_extension(".keys.txt"));
    if (!key) {
        r.error = "no key frame found with ffprobe";
        return r;
    }
    r.start_s = *key;
    r.duration_s = duration_s + (start_s - *key);
    // 1 ms past the key frame: the seek lands on it, not on the one before.
    const std::string cmd = Quoted(tools.ffmpeg) + " -nostdin -y -ss " + Seconds(*key + 0.001) + " -i " +
                            Quoted(in.u8string()) + " -t " + Seconds(r.duration_s) +
                            " -map 0:v:0 -map 0:a? -c copy -avoid_negative_ts make_zero " + Quoted(out.u8string());
    std::error_code ec;
    if (!RunQuiet(cmd) || !std::filesystem::exists(out, ec)) {
        r.error = "ffmpeg could not copy the streams";
        return r;
    }
    r.bytes = std::filesystem::file_size(out, ec);
    r.ok = r.bytes > 0;
    if (!r.ok) r.error = "ffmpeg wrote an empty file";
    return r;
}

// H.264 at a capped bit rate (never wider than max_width), AAC sound. The
// start is exact: ffmpeg decodes from the key frame before it.
inline CutResult CutReencode(const Tools& tools, const std::filesystem::path& in, const std::filesystem::path& out,
                             double start_s, double duration_s, int video_kbps, int audio_kbps, int max_width) {
    CutResult r;
    r.mode = "reencode";
    r.start_s = start_s;
    r.duration_s = duration_s;
    const std::string k = std::to_string(video_kbps);
    const std::string cmd = Quoted(tools.ffmpeg) + " -nostdin -y -ss " + Seconds(start_s) + " -i " +
                            Quoted(in.u8string()) + " -t " + Seconds(duration_s) + " -map 0:v:0 -map 0:a?" +
                            " -vf " + Quoted("scale=min(" + std::to_string(max_width) + "\\,iw):-2") +
                            " -c:v libx264 -preset veryfast -pix_fmt yuv420p -b:v " + k + "k -maxrate " + k +
                            "k -bufsize " + std::to_string(2 * video_kbps) + "k -c:a aac -b:a " +
                            std::to_string(audio_kbps) + "k -movflags +faststart " + Quoted(out.u8string());
    std::error_code ec;
    if (!RunQuiet(cmd) || !std::filesystem::exists(out, ec)) {
        r.error = "ffmpeg could not re-encode (is libx264 in this build?)";
        return r;
    }
    r.bytes = std::filesystem::file_size(out, ec);
    r.ok = r.bytes > 0;
    if (!r.ok) r.error = "ffmpeg wrote an empty file";
    return r;
}

// The dense clip under the cap: a stream copy, shortened once or twice when
// it comes out too large, else the capped re-encode (its rate lowered when
// the first try is still over).
inline CutResult CutUnderCap(const Tools& tools, const std::filesystem::path& in, const std::filesystem::path& copy_out,
                             const std::filesystem::path& reencode_out, const Plan& plan, const PlanOptions& o) {
    const double cap = o.max_mb * 1024 * 1024;
    std::error_code ec;
    CutResult last;
    if (!plan.dense_reencode) {
        double duration = plan.dense_clip.duration_s;
        for (int attempt = 0; attempt < 3 && duration >= o.min_dense_s; ++attempt) {
            last = CutCopy(tools, in, copy_out, plan.dense_clip.start_s, duration);
            if (!last.ok) break;
            if (double(last.bytes) <= cap) return last;
            duration = std::floor(duration * cap / double(last.bytes) * 0.93);
        }
        std::filesystem::remove(copy_out, ec);
    }
    const double seconds = plan.dense_reencode ? plan.dense_clip.duration_s : double(o.dense_s);
    const double start = plan.dense_clip.start_s;
    int kbps = plan.dense_video_kbps > 0
                   ? plan.dense_video_kbps
                   : std::max(150, int(0.92 * cap * 8 / 1000.0 / seconds) - o.audio_kbps);
    for (int attempt = 0; attempt < 3; ++attempt) {
        last = CutReencode(tools, in, reencode_out, start, seconds, kbps, o.audio_kbps, o.reencode_max_width);
        if (!last.ok) return last;
        if (double(last.bytes) <= cap) return last;
        kbps = std::max(100, int(kbps * cap / double(last.bytes) * 0.9));
    }
    std::filesystem::remove(reencode_out, ec);
    last.ok = false;
    last.error = "the clip stayed above the size cap";
    return last;
}

// The script for a cut: its actions inside the window, starting at 0.
// False (with *error) when the script cannot be read; an empty window
// writes nothing and is no error (*written = false).
inline bool WriteCutScript(const std::filesystem::path& script, const std::filesystem::path& out, double start_s,
                           double duration_s, bool* written, std::string* error) {
    *written = false;
    const auto text = samn::ReadFileToString(script);
    if (!text) {
        if (error) *error = "cannot read the script";
        return false;
    }
    try {
        const samn::Script s = samn::Parse(*text);
        samn::Script cut;
        cut.inverted = s.inverted;
        cut.actions = samn::CutScriptWindow(s.actions, int64_t(std::llround(start_s * 1000)),
                                            int64_t(std::llround((start_s + duration_s) * 1000)));
        cut.duration_meta = int64_t(std::llround(duration_s * 1000));
        if (cut.actions.size() < 2) return true;
        samn::WriteFileAtomic(out, samn::Write(cut));
        *written = true;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("the script cannot be used: ") + e.what();
        return false;
    }
}

}  // namespace samn::golden
