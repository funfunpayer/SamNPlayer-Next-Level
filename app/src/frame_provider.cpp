#include "frame_provider.h"

#include <QQuickTextureFactory>
#include <QRunnable>
#include <cmath>

#include "frame_cache.h"

namespace samn::ui {
namespace {

class FrameResponse : public QQuickImageResponse, public QRunnable {
public:
    FrameResponse(QString path, double ms, int height) : path_(std::move(path)), ms_(ms), height_(height) {
        setAutoDelete(false);
    }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }
    QString errorString() const override { return error_; }

    void run() override {
        image_ = FrameCache::shared().At(path_, ms_, height_).image;
        if (image_.isNull()) error_ = QStringLiteral("no frame at %1 ms").arg(ms_);
        emit finished();
    }

private:
    QString path_;
    double ms_;
    int height_;
    QImage image_;
    QString error_;
};

}  // namespace

FrameProvider::FrameProvider() { pool_.setMaxThreadCount(1); }

QString FrameProvider::idFor(const QString& videoPath, double ms, int height) {
    return QStringLiteral("%1/%2/%3").arg(std::llround(ms)).arg(height).arg(QString::fromUtf8(QUrl::toPercentEncoding(videoPath)));
}

QQuickImageResponse* FrameProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    const int a = id.indexOf(QLatin1Char('/'));
    const int b = a < 0 ? -1 : id.indexOf(QLatin1Char('/'), a + 1);
    const double ms = a < 0 ? 0 : id.left(a).toDouble();
    int height = b < 0 ? 0 : id.mid(a + 1, b - a - 1).toInt();
    if (requestedSize.height() > 0) height = requestedSize.height();
    const QString path = b < 0 ? QString() : QUrl::fromPercentEncoding(id.mid(b + 1).toUtf8());
    auto* response = new FrameResponse(path, ms, height);
    pool_.start(response);
    return response;
}

}  // namespace samn::ui
