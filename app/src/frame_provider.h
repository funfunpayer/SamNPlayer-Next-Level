#pragma once

#include <QQuickAsyncImageProvider>
#include <QString>
#include <QThreadPool>
#include <QUrl>

namespace samn::ui {

// image://frames/<ms>/<height>/<percent-encoded video path>: one video frame,
// decoded off the UI thread through FrameCache::shared() (the frame strip).
class FrameProvider : public QQuickAsyncImageProvider {
public:
    FrameProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    // The id for a frame (QML builds the same string with encodeURIComponent).
    static QString idFor(const QString& videoPath, double ms, int height);

private:
    // One thread: frames decode one at a time anyway (one open video), and
    // keeping the strip's order lets the cache read on instead of seeking.
    // Destroyed with the engine; the destructor waits for running decodes.
    QThreadPool pool_;
};

}  // namespace samn::ui
