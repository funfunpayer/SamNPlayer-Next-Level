#include "frame_cache.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <opencv2/imgproc.hpp>

#include "samn/vision/frame_reader.h"

namespace samn::ui {
namespace {

// Reading on is cheaper than a frame-exact seek up to about this far ahead.
constexpr double kReadOnMs = 1500;

std::string Key(const QString& path, double ms, int maxHeight) {
    return path.toStdString() + '|' + std::to_string(std::llround(ms)) + '|' + std::to_string(maxHeight);
}

}  // namespace

FrameCache::FrameCache(size_t budgetBytes)
    : cache_(budgetBytes, [](const Frame& f) { return size_t(f.image.sizeInBytes()) + 64; }) {}

FrameCache::~FrameCache() = default;

FrameCache& FrameCache::shared() {
    static FrameCache cache;
    return cache;
}

bool FrameCache::openLocked(const QString& path) {
    if (reader_ && readerPath_ == path) return true;
    std::string error;
    reader_ = samn::vision::FrameReader::Open(std::filesystem::path(path.toStdU16String()), &error);
    readerPath_ = reader_ ? path : QString();
    lastMs_ = -1;
    return reader_ != nullptr;
}

QImage FrameCache::decodeLocked(double ms, double* actualMs) {
    samn::vision::VideoFrame f;
    const bool readOn = lastMs_ >= 0 && ms > lastMs_ && ms - lastMs_ <= kReadOnMs;
    if (!readOn) {
        ++seeks_;
        if (!reader_->SeekMs(int64_t(std::llround(ms)))) return {};
    }
    // Reading on: skip to the first frame at or after ms.
    do {
        if (!reader_->Read(f)) {
            lastMs_ = -1;
            return {};
        }
    } while (readOn && double(f.pts_us) / 1000.0 + 0.5 < ms);
    lastMs_ = double(f.pts_us) / 1000.0;
    *actualMs = lastMs_;
    cv::Mat rgb;
    cv::cvtColor(f.bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888).copy();
}

FrameCache::Frame FrameCache::At(const QString& path, double ms, int maxHeight) {
    ms = std::max(0.0, ms);
    const std::string key = Key(path, ms, maxHeight);
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto hit = cache_.Get(key)) return *hit;
    Frame out;
    if (!openLocked(path)) return out;
    double actual = ms;
    QImage image = decodeLocked(ms, &actual);
    if (image.isNull()) return out;
    if (maxHeight > 0 && image.height() > maxHeight)
        image = image.scaledToHeight(maxHeight, Qt::SmoothTransformation);
    out.image = std::move(image);
    out.ms = actual;
    cache_.Put(key, out);
    return out;
}

size_t FrameCache::hits() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.hits();
}

size_t FrameCache::misses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.misses();
}

size_t FrameCache::usedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.used();
}

int FrameCache::seeks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return seeks_;
}

}  // namespace samn::ui
