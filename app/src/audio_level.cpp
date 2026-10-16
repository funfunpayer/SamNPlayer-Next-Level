#include "audio_level.h"

#include <QAudioBuffer>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace samn::ui {

AudioLevelDecoder::AudioLevelDecoder(QObject* parent) : QObject(parent) {}
AudioLevelDecoder::~AudioLevelDecoder() { cancel(); }

void AudioLevelDecoder::cancel() {
    if (!decoder_) return;
    decoder_->disconnect(this);
    decoder_->stop();
    decoder_.reset();
    rms_.reset();
}

void AudioLevelDecoder::start(const QString& path, int stepMs) {
    cancel();
    path_ = path;
    stepMs_ = stepMs > 0 ? stepMs : samn::kAudioLevelStepMs;
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        emit failed(tr("Video not found: %1").arg(path));
        return;
    }
    if (path == cachePath_ && fi.size() == cacheSize_ && fi.lastModified() == cacheTime_ && stepMs_ == cacheStep_) {
        emit finished(cache_);
        return;
    }
    decoder_ = std::make_unique<QAudioDecoder>();
    connect(decoder_.get(), &QAudioDecoder::bufferReady, this, &AudioLevelDecoder::onBuffer);
    connect(decoder_.get(), &QAudioDecoder::finished, this, &AudioLevelDecoder::onDone);
    connect(decoder_.get(), qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, &AudioLevelDecoder::onError);
    decoder_->setSource(QUrl::fromLocalFile(path));
    decoder_->start();
}

void AudioLevelDecoder::onBuffer() {
    if (!decoder_) return;
    const QAudioBuffer buf = decoder_->read();
    if (!buf.isValid()) return;
    const QAudioFormat fmt = buf.format();
    const int channels = std::max(1, fmt.channelCount());
    const int bytesPerSample = fmt.bytesPerSample();
    if (bytesPerSample <= 0 || fmt.sampleRate() <= 0) return;
    // The window is fixed by the first buffer's rate (one stream, one rate).
    if (!rms_) rms_.emplace(static_cast<size_t>(std::max<qint64>(1, qint64(fmt.sampleRate()) * stepMs_ / 1000)));
    const auto* data = buf.constData<char>();
    const qsizetype frames = buf.frameCount();
    for (qsizetype f = 0; f < frames; ++f) {
        // Mono mix, as ffmpeg -ac 1 did in 0.7.
        float sum = 0;
        for (int c = 0; c < channels; ++c)
            sum += fmt.normalizedSampleValue(data + (f * channels + c) * bytesPerSample);
        rms_->Push(sum / static_cast<float>(channels));
    }
}

void AudioLevelDecoder::onDone() {
    if (!decoder_) return;
    const auto rms = rms_ ? rms_->Finish() : std::vector<double>{};
    decoder_->disconnect(this);
    decoder_.release()->deleteLater();
    rms_.reset();
    if (rms.empty()) {
        emit failed(tr("No readable sound track"));
        return;
    }
    const QFileInfo fi(path_);
    cachePath_ = path_;
    cacheSize_ = fi.size();
    cacheTime_ = fi.lastModified();
    cacheStep_ = stepMs_;
    cache_ = samn::NormalizeLoudness(rms);
    emit finished(cache_);
}

void AudioLevelDecoder::onError() {
    if (!decoder_) return;
    const QString msg = decoder_->errorString();
    decoder_->disconnect(this);
    decoder_.release()->deleteLater();
    rms_.reset();
    emit failed(msg.isEmpty() ? tr("No readable sound track") : msg);
}

AudioLevelItem::AudioLevelItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &AudioLevelItem::viewChanged, this, [this] { update(); });
    connect(&decoder_, &AudioLevelDecoder::finished, this, [this](const std::vector<double>& env) {
        error_.clear();
        setLevel(env);
        emit loadingChanged();
    });
    connect(&decoder_, &AudioLevelDecoder::failed, this, [this](const QString& e) {
        error_ = e;
        setLevel({});
        emit loadingChanged();
    });
}

void AudioLevelItem::setSource(const QUrl& url) {
    if (url == source_) return;
    source_ = url;
    emit sourceChanged();
    setLevel({});
    if (url.isEmpty()) {
        decoder_.cancel();
    } else {
        decoder_.start(url.isLocalFile() ? url.toLocalFile() : url.toString());
    }
    emit loadingChanged();
}

void AudioLevelItem::setLevel(std::vector<double> level) {
    level_ = std::move(level);
    emit levelChanged();
    update();
}

void AudioLevelItem::paint(QPainter* p) {
    const double w = width(), h = height();
    if (w <= 0 || h <= 0 || level_.size() < 2) return;
    const double t0 = viewStart_;
    const double t1 = viewEnd_ > viewStart_ ? viewEnd_ : double(level_.size() * samn::kAudioLevelStepMs);
    const double span = std::max(1.0, t1 - t0);
    const double mid = h / 2, half = h / 2 - 3;
    const int step = 2;
    QColor c = color_;
    c.setAlphaF(0.85);
    const auto n = static_cast<long long>(level_.size());
    for (int x = 0; x < int(w); x += step) {
        // Script time → video time: + offset.
        const double a = t0 + (x / w) * span + offset_, b = t0 + ((x + step) / w) * span + offset_;
        long long i0 = static_cast<long long>(std::floor(a / samn::kAudioLevelStepMs));
        long long i1 = std::max(i0 + 1, static_cast<long long>(std::ceil(b / samn::kAudioLevelStepMs)));
        if (i0 >= n) break;
        if (i1 <= 0) continue;
        i0 = std::max(0LL, i0);
        double peak = 0;
        for (long long i = i0; i < std::min(i1, n); ++i) peak = std::max(peak, level_[size_t(i)]);
        const double hg = std::max(0.6, peak * half);
        p->fillRect(QRectF(x, mid - hg, step - 0.6, hg * 2), c);
    }
    if (!label_.isEmpty()) {
        QFont f(QStringLiteral("Figtree"));
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(color_);
        p->drawText(QPointF(6, 13), label_);
    }
    if (playhead_ > 0) {
        const double x = std::round((playhead_ - t0) / span * w) + 0.5;
        p->setPen(QPen(QColor(255, 255, 255), 1.25));
        p->drawLine(QPointF(x, 0), QPointF(x, h));
    }
}

}  // namespace samn::ui
