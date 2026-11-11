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
#include "samn/audio_onsets.h"
#include "samn/audio_tempo.h"

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
    // unchanged file, step, and length cap are served from the cache.
    // maxMs < 0 reads the whole file. maxMs >= 0 stops once the RMS windows
    // cover that many milliseconds (windows * stepMs >= maxMs) and finishes
    // the same way as end-of-file.
    // withOnsets streams an OnsetCurve + TempoAccumulator in the same pass
    // (drained; no whole-file PCM). onsetTempo / onsetBlocks are filled when
    // that decode (or its cache) had onsets; otherwise they stay empty and
    // "BPM from audio" falls back to loudness.
    void start(const QString& path, int stepMs = samn::kAudioLevelStepMs, qint64 maxMs = -1,
               bool withOnsets = false);
    void cancel();
    bool busy() const { return decoder_ != nullptr; }
    const samn::TempoEstimate& onsetTempo() const { return onsetTempo_; }
    const std::vector<samn::AudioBlock>& onsetBlocks() const { return onsetBlocks_; }

signals:
    void finished(const std::vector<double>& envelope);
    void failed(const QString& error);

private:
    void onBuffer();
    void onDone();
    void onError();
    // Shared end of a decode. stopDecoder halts a still-running backend
    // (the length cap); end-of-file has already stopped.
    void finishDecode(bool stopDecoder);

    std::unique_ptr<QAudioDecoder> decoder_;
    std::optional<samn::WindowRms> rms_;
    std::optional<samn::OnsetCurve> onsets_;
    std::optional<samn::TempoAccumulator> tempoAcc_;
    QString path_;
    int stepMs_ = samn::kAudioLevelStepMs;
    qint64 maxMs_ = -1;
    bool withOnsets_ = false;
    samn::TempoEstimate onsetTempo_;
    std::vector<samn::AudioBlock> onsetBlocks_;
    // Cache: the last decoded file. cacheMaxMs_ < 0 is a full-file decode.
    QString cachePath_;
    qint64 cacheSize_ = -1;
    QDateTime cacheTime_;
    int cacheStep_ = 0;
    qint64 cacheMaxMs_ = -1;
    bool cacheHasOnsets_ = false;
    std::vector<double> cache_;
    samn::TempoEstimate cacheOnsetTempo_;
    std::vector<samn::AudioBlock> cacheOnsetBlocks_;
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
    // Beat grid period in ms. 0 draws no lines (grid off, or no BPM).
    Q_PROPERTY(double beatPeriodMs MEMBER beatPeriod_ NOTIFY viewChanged)
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
    double viewStart_ = 0, viewEnd_ = -1, playhead_ = -1, beatPeriod_ = 0;
    int offset_ = 0;
    QColor color_{0x93, 0xae, 0xc4};
};

}  // namespace samn::ui
