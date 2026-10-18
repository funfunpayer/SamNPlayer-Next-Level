#pragma once

#include <QImage>
#include <QString>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "samn/lru_cache.h"

namespace samn::vision {
class FrameReader;
}

namespace samn::ui {

// Video frames for the Editor: the preview frame and the frame strip. Keeps
// the video open (opening costs far more than a seek) and keeps decoded
// frames in a cache with a hard size limit (D-142). Thread-safe; decoding
// is one at a time per cache.
class FrameCache {
public:
    struct Frame {
        QImage image;      // RGB; empty when the frame cannot be read
        double ms = -1;    // the frame's own time (at or after the asked time)
    };

    explicit FrameCache(size_t budgetBytes = size_t{96} << 20);
    ~FrameCache();
    FrameCache(const FrameCache&) = delete;
    FrameCache& operator=(const FrameCache&) = delete;

    // The app-wide cache that the preview and the frame strip share.
    static FrameCache& shared();

    // The frame at ms, scaled to at most maxHeight pixels high (0 = full
    // size). Reads forward instead of seeking when ms lies a little ahead of
    // the last decoded frame (the strip, a drag).
    Frame At(const QString& path, double ms, int maxHeight = 0);

    size_t hits() const;
    size_t misses() const;
    size_t usedBytes() const;
    int seeks() const;

private:
    bool openLocked(const QString& path);
    QImage decodeLocked(double ms, double* actualMs);

    mutable std::mutex mutex_;
    LruCache<std::string, Frame> cache_;
    std::unique_ptr<samn::vision::FrameReader> reader_;
    QString readerPath_;
    double lastMs_ = -1;  // time of the last decoded frame
    int seeks_ = 0;
};

}  // namespace samn::ui
