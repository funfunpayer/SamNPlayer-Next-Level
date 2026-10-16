#pragma once

#include <QAudioDecoder>
#include <QDateTime>
#include <QObject>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>
#include <vector>

#include "samn/audio_envelope.h"

namespace samn::ui {

// Decodes the sound of a video with Qt Multimedia (no ffmpeg process) and
// returns its loudness envelope, 0..1 per 50 ms of video time
// (samn/audio_envelope.h). Asynchronous; the decoder runs in the
// multimedia backend and the windows are summed as the buffers arrive.
class AudioLevelDecoder : public QObject {
    Q_OBJECT
public:
    explicit AudioLevelDecoder(QObject* parent = nullptr);
    ~AudioLevelDecoder() override;
    // Starts decoding; a running decode is cancelled. Results for the same
    // unchanged file are served from the cache.
    void start(const QString& path, int stepMs = samn::kAudioLevelStepMs);
    void cancel();
    bool busy() const { return decoder_ != nullptr; }

signals:
    void finished(const std::vector<double>& envelope);
    void failed(const QString& error);

private:
    void onBuffer();
    void onDone();
    void onError();

    std::unique_ptr<QAudioDecoder> decoder_;
    std::optional<samn::WindowRms> rms_;
    QString path_;
    int stepMs_ = samn::kAudioLevelStepMs;
    // Cache: the last decoded file.
    QString cachePath_;
    qint64 cacheSize_ = -1;
    QDateTime cacheTime_;
    int cacheStep_ = 0;
    std::vector<double> cache_;
};

// The Audio Level lane under the curve (0.7 drawAudioLane): the loudness of
// the video's sound as mirrored bars, peak per 2 px column, on the same
// time axis as the curve lanes. The envelope is in video time, so the lane
// shifts by the script offset and stays under the strokes it belongs to.
class AudioLevelItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(double viewStartMs MEMBER viewStart_ NOTIFY viewChanged)
    Q_PROPERTY(double viewEndMs MEMBER viewEnd_ NOTIFY viewChanged)
    Q_PROPERTY(int offsetMs MEMBER offset_ NOTIFY viewChanged)
    Q_PROPERTY(double playheadMs MEMBER playhead_ NOTIFY viewChanged)
    Q_PROPERTY(QColor color MEMBER color_ NOTIFY viewChanged)
    Q_PROPERTY(QString label MEMBER label_ NOTIFY viewChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY levelChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY levelChanged)
public:
    explicit AudioLevelItem(QQuickItem* parent = nullptr);
    QUrl source() const { return source_; }
    void setSource(const QUrl& url);
    bool empty() const { return level_.size() < 2; }
    bool loading() const { return decoder_.busy(); }
    QString error() const { return error_; }
    const std::vector<double>& level() const { return level_; }
    void setLevel(std::vector<double> level);
    void paint(QPainter* p) override;

signals:
    void sourceChanged();
    void viewChanged();
    void levelChanged();
    void loadingChanged();

private:
    AudioLevelDecoder decoder_;
    QUrl source_;
    std::vector<double> level_;
    QString error_, label_ = QStringLiteral("AUDIO LEVEL");
    double viewStart_ = 0, viewEnd_ = -1, playhead_ = -1;
    int offset_ = 0;
    QColor color_{0x93, 0xae, 0xc4};
};

}  // namespace samn::ui
