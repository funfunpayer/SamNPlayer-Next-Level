#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "samn/funscript.h"

namespace samn::ui {

class CurveItem;

// Play mode logic for the QML page: opens scripts and videos, keeps the
// offset and maps video time to script time. The MediaPlayer itself lives in
// QML; it reports its position through setVideoPositionMs().
class PlayerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString scriptPath READ scriptPath NOTIFY scriptChanged)
    Q_PROPERTY(QString scriptName READ scriptName NOTIFY scriptChanged)
    Q_PROPERTY(QUrl videoUrl READ videoUrl NOTIFY videoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoChanged)
    Q_PROPERTY(bool hasScript READ hasScript NOTIFY scriptChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY scriptChanged)
    Q_PROPERTY(qint64 scriptDurationMs READ scriptDurationMs NOTIFY scriptChanged)
    Q_PROPERTY(int offsetMs READ offsetMs WRITE setOffsetMs NOTIFY offsetMsChanged)
    Q_PROPERTY(qint64 playheadMs READ playheadMs NOTIFY playheadMsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kOffsetLimitMs = 10000;

    explicit PlayerController(QObject* parent = nullptr);

    // A video or a script (local path or file URL). Returns false and sets
    // lastError when it cannot be opened.
    Q_INVOKABLE bool openPath(const QString& pathOrUrl);
    Q_INVOKABLE bool openScript(const QString& path, const QString& video = {});
    Q_INVOKABLE void setVideoPositionMs(qint64 videoMs);
    Q_INVOKABLE void attachCurve(samn::ui::CurveItem* curve);
    Q_INVOKABLE QString formatTime(qint64 ms) const;

    QString scriptPath() const { return scriptPath_; }
    QString scriptName() const;
    QUrl videoUrl() const;
    QString videoName() const;
    bool hasScript() const { return !actions_.empty(); }
    int pointCount() const { return static_cast<int>(actions_.size()); }
    qint64 scriptDurationMs() const { return actions_.empty() ? 0 : actions_.back().at; }
    int offsetMs() const { return offsetMs_; }
    void setOffsetMs(int ms);
    qint64 playheadMs() const { return playheadMs_; }
    QString lastError() const { return lastError_; }
    QString videoPath() const { return videoPath_; }
    const std::vector<samn::Action>& actions() const { return actions_; }

    static QString toLocalPath(const QString& pathOrUrl);

signals:
    void scriptChanged();
    void videoChanged();
    void offsetMsChanged();
    void playheadMsChanged();
    void lastErrorChanged();

private:
    void setVideo(const QString& path);
    void setError(const QString& e);
    void pushToCurve();

    std::vector<samn::Action> actions_;
    QString scriptPath_, videoPath_, lastError_;
    int offsetMs_ = 0;
    qint64 videoMs_ = 0, playheadMs_ = -1;
    QPointer<CurveItem> curve_;
};

}  // namespace samn::ui
