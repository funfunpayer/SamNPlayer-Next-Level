#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace samn::ui {

// The plugin switches (D-155): extra tabs after Player · Generator · Training.
// A mode is a QML page in the app's module and a descriptor in the ModeRegistry
// (D-165); it shows only while its switch in Settings → General is on. One
// instance for the app (a QML singleton), so Settings sees the same state.
class Plugins : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // AI Train and Benchmark (0.7 Suite tools), a preview: off by default.
    Q_PROPERTY(bool aiTrainEnabled READ aiTrainEnabled WRITE setAiTrainEnabled NOTIFY changed)
public:
    explicit Plugins(QObject* parent = nullptr);
    bool aiTrainEnabled() const { return ai_train_; }
    void setAiTrainEnabled(bool on);
    // The saved switch, read from the settings (what a new start shows).
    static bool aiTrainStored();

signals:
    void changed();

private:
    bool ai_train_ = false;
};

}  // namespace samn::ui
