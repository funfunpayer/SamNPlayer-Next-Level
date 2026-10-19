#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace samn::ui {

// The plugin modes (D-155): extra tabs after Player · Generator · Training.
// A mode is a QML page in the app's module; it shows only while its switch in
// Settings → General is on. One instance for the app (a QML singleton), so the
// tab row and Settings see the same state.
class Plugins : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // AI Train and Benchmark (0.7 Suite tools), a preview: off by default.
    Q_PROPERTY(bool aiTrainEnabled READ aiTrainEnabled WRITE setAiTrainEnabled NOTIFY changed)
    // The enabled modes: {id, name, icon, page} in tab order.
    Q_PROPERTY(QVariantList modes READ modes NOTIFY changed)
public:
    explicit Plugins(QObject* parent = nullptr);
    bool aiTrainEnabled() const { return ai_train_; }
    void setAiTrainEnabled(bool on);
    QVariantList modes() const;

signals:
    void changed();

private:
    bool ai_train_ = false;
};

}  // namespace samn::ui
