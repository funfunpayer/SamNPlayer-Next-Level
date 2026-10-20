#pragma once
// AI Train step 5 (D-155): the student. One click sets up a Python of the
// app's own (uv, fetched like NudeNet with pinned hashes; Python 3.12,
// PyTorch and rfdetr in config_dir()/ai, nothing of the system touched); a
// second trains RF-DETR on the clips added to the training set
// (config_dir()/ai/datasets/<clip>) with ai/train_student.py, with progress
// and Stop. The result is config_dir()/models/student/ (student.onnx and
// classes.json), which the label editor can use as a teacher.

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

#include "model_download.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace samn {
class Settings;
}

namespace samn::ui {

class TrainController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // The environment: ready, being set up (with a line on what it does).
    Q_PROPERTY(bool envReady READ envReady NOTIFY envChanged)
    Q_PROPERTY(bool settingUp READ settingUp NOTIFY envChanged)
    Q_PROPERTY(QString envStatus READ envStatus NOTIFY envChanged)
    // "auto" (NVIDIA -> CUDA, else CPU), "cuda" or "cpu"; aitrain.student.device.
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY settingsChanged)
    Q_PROPERTY(int epochs READ epochs WRITE setEpochs NOTIFY settingsChanged)
    // The clips in the training set: [{name, samples}].
    Q_PROPERTY(QVariantList datasets READ datasets NOTIFY datasetsChanged)
    // A training run: busy, 0..100, a line on where it is.
    Q_PROPERTY(bool training READ training NOTIFY trainChanged)
    Q_PROPERTY(int trainProgress READ trainProgress NOTIFY trainChanged)
    Q_PROPERTY(QString trainStatus READ trainStatus NOTIFY trainChanged)
    // The student model ("" until one is trained) and its classes.
    Q_PROPERTY(QString studentPath READ studentPath NOTIFY trainChanged)
    Q_PROPERTY(QStringList studentClasses READ studentClasses NOTIFY trainChanged)

public:
    explicit TrainController(QObject* parent = nullptr);
    ~TrainController() override;

    bool envReady() const;
    bool settingUp() const { return setting_up_; }
    QString envStatus() const { return env_status_; }
    QString device() const { return device_; }
    void setDevice(const QString& d);
    int epochs() const { return epochs_; }
    void setEpochs(int e);
    QVariantList datasets() const;
    bool training() const { return train_ != nullptr; }
    int trainProgress() const { return train_progress_; }
    QString trainStatus() const { return train_status_; }
    QString studentPath() const;
    QStringList studentClasses() const;

    // Fetches uv and sets up Python, PyTorch and rfdetr (about 0.3 GB on a
    // CPU, about 3 GB with CUDA). Asynchronous; Cancel stops it.
    Q_INVOKABLE bool setUpEnvironment();
    Q_INVOKABLE void cancelSetup();
    // Trains on every clip of the training set. Asynchronous.
    Q_INVOKABLE bool train();
    // Ends the run after the current step and keeps what was learned; a
    // second call stops it at once.
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool removeDataset(const QString& name);

    // Where things live (all under config_dir()/ai, the model under models/).
    static QString aiDir();
    static QString datasetsDir();
    static QString studentDir();
    QString envPython() const;

    // Tests: uv from elsewhere (a program and its first arguments) instead
    // of the download, and the training script.
    void setUvCommand(const QString& program, const QStringList& prefix) {
        uv_program_ = program;
        uv_prefix_ = prefix;
    }
    void setUvSource(const ModelSource& s) { uv_source_ = s; }
    void setTrainScript(const QString& path) { script_ = path; }

signals:
    void envChanged();
    void settingsChanged();
    void datasetsChanged();
    void trainChanged();

private:
    struct Step {
        QString what;
        QString program;
        QStringList args;
    };
    void runSteps(QList<Step> steps, std::function<void()> done);
    void finishSetup(bool ok, const QString& status);
    void setEnvStatus(const QString& s);
    void setTrainStatus(const QString& s);
    void onTrainLine(const QByteArray& line);
    QProcessEnvironment uvEnvironment() const;
    samn::Settings& settings();

    std::unique_ptr<samn::Settings> settings_;
    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* download_ = nullptr;
    QProcess* step_ = nullptr;
    QProcess* train_ = nullptr;
    bool setting_up_ = false, stop_asked_ = false;
    QString env_status_, train_status_, device_ = QStringLiteral("auto"), run_dir_, uv_program_, script_;
    QStringList uv_prefix_;
    ModelSource uv_source_;
    int epochs_ = 50, train_progress_ = 0;
    QByteArray train_buffer_;
    QStringList train_tail_;
};

// uv 0.13.4 for this platform (the PyPI wheel, both hashes pinned); an empty
// url where there is none.
ModelSource UvSource();

}  // namespace samn::ui
