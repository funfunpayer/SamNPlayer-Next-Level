#include "train_controller.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

#include "samn/settings.h"

namespace samn::ui {

namespace {
constexpr const char* kRfdetr = "rfdetr[train,onnx]==1.11.2";
constexpr const char* kPython = "3.12";
QString FromFs(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }

// The last line of what a process said, for the status.
QString LastLine(const QByteArray& out) {
    const QList<QByteArray> lines = out.trimmed().split('\n');
    return lines.isEmpty() ? QString() : QString::fromUtf8(lines.last()).trimmed();
}
}  // namespace

ModelSource UvSource() {
#if defined(Q_OS_WIN) && (defined(Q_PROCESSOR_X86_64))
    return {QStringLiteral("uv 0.13.4"),
            QUrl(QStringLiteral("https://files.pythonhosted.org/packages/2f/b9/b526eab6c72eacc705a58378f6850fde046b28c86fed"
                                "01bd42da61367c6b/uv-0.13.4-py3-none-win_amd64.whl")),
            "b2ff80397273fff0e02c20eacae6882834c811827d6fc08f658db156fe754386",
            QStringLiteral("uv-0.13.4.data/scripts/uv.exe"),
            "b9d9656370ee1812e53e985d8bbb81870038106df98ef34cb45bcf4e0fe0df17", QStringLiteral("uv.exe")};
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    return {QStringLiteral("uv 0.13.4"),
            QUrl(QStringLiteral("https://files.pythonhosted.org/packages/4b/a1/de02c0913726728eca56ee258875cbba9c7b0572c63f"
                                "494fd9f55c7852e3/uv-0.13.4-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl")),
            "05e4332791b4d55b6dde8695b7fa6079b26991519f44760887c5dfbd5ca4fe53",
            QStringLiteral("uv-0.13.4.data/scripts/uv"),
            "5b2a2df4ef04c5d778d50702d6e1d1ff22909fc883086b4ef406a4f115e3c4fb", QStringLiteral("uv")};
#else
    return {};
#endif
}

TrainController::TrainController(QObject* parent) : QObject(parent), uv_source_(UvSource()) {
    try {
        const auto d = settings().get("aitrain.student.device", "auto");
        if (d.is_string()) device_ = QString::fromStdString(d.get<std::string>());
        const auto e = settings().get("aitrain.student.epochs", 50);
        if (e.is_number_integer()) epochs_ = std::clamp(e.get<int>(), 1, 1000);
    } catch (const std::exception&) {
    }
    if (device_ != "cuda" && device_ != "cpu") device_ = QStringLiteral("auto");
#ifdef SAMN_SOURCE_AI_DIR
    const QString dev = QStringLiteral(SAMN_SOURCE_AI_DIR) + "/train_student.py";
#else
    const QString dev;
#endif
    const QString shipped = QCoreApplication::applicationDirPath() + "/ai/train_student.py";
    script_ = QFileInfo::exists(shipped) ? shipped : dev;
}

TrainController::~TrainController() {
    for (QProcess* p : {step_, train_})
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(3000);
        }
    if (download_) {
        download_->disconnect(this);
        download_->abort();
    }
}

samn::Settings& TrainController::settings() {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

QString TrainController::aiDir() { return FromFs(samn::config_dir() / "ai"); }
QString TrainController::datasetsDir() { return aiDir() + "/datasets"; }
QString TrainController::studentDir() { return FromFs(samn::config_dir() / "models" / "student"); }

QString TrainController::envPython() const {
#ifdef Q_OS_WIN
    return aiDir() + "/train-env/Scripts/python.exe";
#else
    return aiDir() + "/train-env/bin/python";
#endif
}

bool TrainController::envReady() const { return QFileInfo::exists(aiDir() + "/train-env/samn-ready.json") && QFileInfo::exists(envPython()); }

void TrainController::setDevice(const QString& d) {
    const QString v = (d == "cuda" || d == "cpu") ? d : QStringLiteral("auto");
    if (v == device_) return;
    device_ = v;
    try {
        settings().set("aitrain.student.device", v.toStdString());
    } catch (const std::exception&) {
    }
    emit settingsChanged();
}

void TrainController::setEpochs(int e) {
    e = std::clamp(e, 1, 1000);
    if (e == epochs_) return;
    epochs_ = e;
    try {
        settings().set("aitrain.student.epochs", e);
    } catch (const std::exception&) {
    }
    emit settingsChanged();
}

QVariantList TrainController::datasets() const {
    QVariantList out;
    const QDir dir(datasetsDir());
    for (const QFileInfo& d : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!QFileInfo::exists(d.filePath() + "/classes.txt")) continue;
        const int n = int(QDir(d.filePath() + "/images").entryList(QDir::Files).size());
        out << QVariantMap{{"name", d.fileName()}, {"samples", n}};
    }
    return out;
}

bool TrainController::removeDataset(const QString& name) {
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.startsWith('.')) return false;
    QDir d(datasetsDir() + "/" + name);
    if (!d.exists() || !d.removeRecursively()) return false;
    emit datasetsChanged();
    return true;
}

QString TrainController::studentPath() const {
    const QString p = studentDir() + "/student.onnx";
    return QFileInfo::exists(p) ? p : QString();
}

QStringList TrainController::studentClasses() const {
    QFile f(studentDir() + "/classes.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value("classes").toArray()) out << v.toString();
    return out;
}

void TrainController::setEnvStatus(const QString& s) {
    env_status_ = s;
    emit envChanged();
}

void TrainController::setTrainStatus(const QString& s) {
    train_status_ = s;
    emit trainChanged();
}

QProcessEnvironment TrainController::uvEnvironment() const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Everything in the AI folder; no uv or Python of the system is used.
    env.insert("UV_PYTHON_INSTALL_DIR", aiDir() + "/python");
    env.insert("UV_CACHE_DIR", aiDir() + "/cache");
    env.insert("UV_TOOL_DIR", aiDir() + "/tools");
    env.insert("UV_PYTHON_PREFERENCE", "only-managed");
    env.insert("UV_NO_CONFIG", "1");
    env.remove("VIRTUAL_ENV");
    env.remove("PYTHONPATH");
    env.remove("PYTHONHOME");
    return env;
}

bool TrainController::setUpEnvironment() {
    if (setting_up_ || train_) return false;
    QDir().mkpath(aiDir());
    setting_up_ = true;
    const auto install = [this](const QString& uv, const QStringList& prefix) {
        const QString env = aiDir() + "/train-env";
        const QString backend = device_ == "cpu" ? QStringLiteral("cpu") : QStringLiteral("auto");
        QList<Step> steps{
            {tr("Fetching Python %1 …").arg(kPython), uv, prefix + QStringList{"python", "install", kPython}},
            {tr("Making the training environment …"), uv, prefix + QStringList{"venv", "--clear", "--python", kPython, env}},
            {tr("Installing PyTorch and RF-DETR (large, a few minutes) …"), uv,
             prefix + QStringList{"pip", "install", "--python", envPython(), "--torch-backend", backend, kRfdetr}},
            {tr("Checking the environment …"), envPython(),
             {"-c", "import json, torch, rfdetr; print(json.dumps({'torch': torch.__version__, "
                    "'cuda': bool(torch.cuda.is_available())}))"}}};
        runSteps(steps, [this] {});
    };
    if (!uv_program_.isEmpty()) {  // tests
        install(uv_program_, uv_prefix_);
        emit envChanged();
        return true;
    }
    if (uv_source_.url.isEmpty()) {
        finishSetup(false, tr("Training is not available on this system yet (Windows and Linux x64)."));
        return false;
    }
    const QString uv = aiDir() + "/tools/" + uv_source_.file_name;
    if (QFileInfo::exists(uv)) {
        install(uv, {});
        emit envChanged();
        return true;
    }
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkRequest req(uv_source_.url);
    req.setTransferTimeout(60000);
    download_ = net_->get(req);
    setEnvStatus(tr("Fetching %1 …").arg(uv_source_.name));
    connect(download_, &QNetworkReply::finished, this, [this, uv, install] {
        QNetworkReply* r = download_;
        download_ = nullptr;
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            finishSetup(false, r->error() == QNetworkReply::OperationCanceledError ? tr("Stopped.")
                                                                                    : tr("The download failed: %1").arg(r->errorString()));
            return;
        }
        QString error;
        const auto bin = ExtractModel(r->readAll(), uv_source_, &error);
        if (!bin) {
            finishSetup(false, tr("uv cannot be used: %1").arg(error));
            return;
        }
        QDir().mkpath(QFileInfo(uv).path());
        QSaveFile f(uv);
        if (!f.open(QIODevice::WriteOnly) || f.write(*bin) != bin->size() || !f.commit()) {
            finishSetup(false, tr("Cannot write %1.").arg(QDir::toNativeSeparators(uv)));
            return;
        }
        QFile::setPermissions(uv, QFile::permissions(uv) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        install(uv, {});
    });
    emit envChanged();
    return true;
}

void TrainController::runSteps(QList<Step> steps, std::function<void()> done) {
    if (steps.isEmpty()) {
        // The last step printed the check; it marks the environment ready.
        QSaveFile f(aiDir() + "/train-env/samn-ready.json");
        if (f.open(QIODevice::WriteOnly)) {
            f.write(env_status_.toUtf8());
            f.commit();
        }
        const QJsonObject o = QJsonDocument::fromJson(env_status_.toUtf8()).object();
        finishSetup(true, o.value("cuda").toBool() ? tr("Ready: PyTorch %1 with the NVIDIA card (CUDA).").arg(o.value("torch").toString())
                                                   : tr("Ready: PyTorch %1 on the processor.").arg(o.value("torch").toString()));
        done();
        return;
    }
    const Step s = steps.takeFirst();
    setEnvStatus(s.what);
    step_ = new QProcess(this);
    step_->setProcessEnvironment(uvEnvironment());
    step_->setProcessChannelMode(QProcess::MergedChannels);
    connect(step_, &QProcess::finished, this, [this, steps, done, s](int code, QProcess::ExitStatus status) {
        QProcess* p = step_;
        step_ = nullptr;
        p->deleteLater();
        const QByteArray out = p->readAll();
        if (!setting_up_) return;  // canceled
        if (status != QProcess::NormalExit || code != 0) {
            finishSetup(false, tr("%1 failed: %2").arg(s.what.chopped(2), LastLine(out)));
            return;
        }
        if (steps.isEmpty()) env_status_ = LastLine(out);  // the check's JSON
        runSteps(steps, done);
    });
    connect(step_, &QProcess::errorOccurred, this, [this, s](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && step_) {
            QProcess* p = step_;
            step_ = nullptr;
            p->deleteLater();
            finishSetup(false, tr("%1 failed: cannot start %2").arg(s.what.chopped(2), QDir::toNativeSeparators(s.program)));
        }
    });
    step_->start(s.program, s.args);
}

void TrainController::cancelSetup() {
    if (!setting_up_) return;
    if (download_) download_->abort();
    if (step_) {
        QProcess* p = step_;
        step_ = nullptr;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(3000);
        p->deleteLater();
    }
    finishSetup(false, tr("Stopped."));
}

void TrainController::finishSetup(bool ok, const QString& status) {
    setting_up_ = false;
    if (!ok) QFile::remove(aiDir() + "/train-env/samn-ready.json");
    setEnvStatus(status);
}

bool TrainController::train() {
    if (train_ || setting_up_) return false;
    if (!envReady()) {
        setTrainStatus(tr("Set up the training first."));
        return false;
    }
    if (script_.isEmpty() || !QFileInfo::exists(script_)) {
        setTrainStatus(tr("The training script is missing (ai/train_student.py beside the app)."));
        return false;
    }
    QStringList data;
    int samples = 0;
    for (const QVariant& v : datasets()) {
        const QVariantMap d = v.toMap();
        if (d.value("samples").toInt() == 0) continue;
        data << datasetsDir() + "/" + d.value("name").toString();
        samples += d.value("samples").toInt();
    }
    if (samples < 10) {
        setTrainStatus(tr("Add more to the training set first (at least 10 samples; the label editor: Add to training set)."));
        return false;
    }
    run_dir_ = aiDir() + "/runs/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QDir().mkpath(run_dir_);
    train_ = new QProcess(this);
    QProcessEnvironment env = uvEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    train_->setProcessEnvironment(env);
    train_->setWorkingDirectory(run_dir_);
    train_->setStandardErrorFile(run_dir_ + "/train.log");
    stop_asked_ = false;
    train_progress_ = 0;
    train_buffer_.clear();
    train_tail_.clear();
    connect(train_, &QProcess::readyReadStandardOutput, this, [this] {
        train_buffer_ += train_->readAllStandardOutput();
        for (int nl; (nl = int(train_buffer_.indexOf('\n'))) >= 0;) {
            onTrainLine(train_buffer_.left(nl).trimmed());
            train_buffer_.remove(0, nl + 1);
        }
    });
    connect(train_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        QProcess* p = train_;
        if (!train_buffer_.trimmed().isEmpty()) onTrainLine(train_buffer_.trimmed());
        train_ = nullptr;
        p->deleteLater();
        if (code != 0 && !train_status_.startsWith(tr("Error"))) {
            QFile log(run_dir_ + "/train.log");
            QString last;
            if (log.open(QIODevice::ReadOnly)) last = LastLine(log.readAll().right(4000));
            setTrainStatus(stop_asked_ ? tr("Stopped.") : tr("Error: training ended (%1).").arg(last.isEmpty() ? QString::number(code) : last));
        }
        emit trainChanged();
    });
    QStringList args{"-u", script_, "--out", run_dir_, "--epochs", QString::number(epochs_), "--device", device_, "--data"};
    args << data;
    setTrainStatus(tr("Starting the training on %n sample(s) …", "", samples));
    train_->start(envPython(), args);
    return true;
}

void TrainController::onTrainLine(const QByteArray& line) {
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    const QString ev = o.value("event").toString();
    if (ev == "start") {
        setTrainStatus(tr("Training on %1 (%2 samples, %3 to check) …")
                           .arg(o.value("device").toString() == "cuda" ? tr("the NVIDIA card") : tr("the processor"))
                           .arg(o.value("train").toInt())
                           .arg(o.value("valid").toInt()));
    } else if (ev == "progress" || ev == "epoch") {
        const int epochs = std::max(1, o.value("epochs").toInt(1));
        const int epoch = std::clamp(o.value("epoch").toInt(1), 1, epochs);
        const int steps = std::max(1, o.value("steps").toInt(1));
        const double within = ev == "epoch" ? 1.0 : double(o.value("step").toInt()) / steps;
        train_progress_ = std::clamp(int(((epoch - 1) + within) * 100.0 / epochs), 0, 99);
        QString s = tr("Epoch %1 of %2").arg(epoch).arg(epochs);
        if (ev == "epoch" && o.value("map50").isDouble())
            s += tr(" — found %1 % of the boxes it checks").arg(int(std::round(o.value("map50").toDouble() * 100)));
        if (stop_asked_) s = tr("Stopping after this step …");
        setTrainStatus(s);
    } else if (ev == "done") {
        // The new student replaces the last one, both files at once.
        const QString dir = studentDir();
        QDir().mkpath(dir);
        bool ok = true;
        for (const QString& name : {QStringLiteral("student.onnx"), QStringLiteral("classes.json")}) {
            QFile::remove(dir + "/" + name + ".new");
            ok = ok && QFile::copy(run_dir_ + "/" + name, dir + "/" + name + ".new");
        }
        for (const QString& name : {QStringLiteral("student.onnx"), QStringLiteral("classes.json")}) {
            if (!ok) break;
            QFile::remove(dir + "/" + name);
            ok = QFile::rename(dir + "/" + name + ".new", dir + "/" + name);
        }
        // The run keeps its log; its copy of the data, the checkpoints (each
        // over 100 MB) and the export go once the student is kept.
        if (ok) {
            for (const QString& sub : {QStringLiteral("dataset"), QStringLiteral("run"), QStringLiteral("export")})
                QDir(run_dir_ + "/" + sub).removeRecursively();
            QFile::remove(run_dir_ + "/student.onnx");
        }
        train_progress_ = 100;
        setTrainStatus(!ok ? tr("Error: the model cannot be kept in %1.").arg(QDir::toNativeSeparators(dir))
                       : o.value("stopped").toBool() ? tr("Stopped early; the student so far is kept.")
                                                    : tr("Done: the student is ready as a teacher."));
    } else if (ev == "error") {
        setTrainStatus(tr("Error: %1").arg(o.value("message").toString()));
    }
}

void TrainController::stop() {
    if (!train_) return;
    if (stop_asked_) {  // the second time: at once
        train_->kill();
        return;
    }
    stop_asked_ = true;
    QFile f(run_dir_ + "/STOP");
    f.open(QIODevice::WriteOnly);
    setTrainStatus(tr("Stopping after this step …"));
}

}  // namespace samn::ui
