#pragma once
// Error toasts (0.7 notify.js showToast): a failure the user must see shows
// for 8 s at the bottom right, clipped at 220 characters, with *Open Log*;
// a click dismisses it, at most 3 show. Everything else goes to the Log tab
// and the area's own status line only. Fed by AppLog::reportError.

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QTimer;

namespace samn::ui {

class Notices : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // [{id, text}], oldest first.
    Q_PROPERTY(QVariantList toasts READ toasts NOTIFY toastsChanged)

public:
    explicit Notices(QObject* parent = nullptr);

    static constexpr int kMax = 3;
    static constexpr int kShowMs = 8000;
    static constexpr int kMaxChars = 220;

    QVariantList toasts() const;
    // An error from QML (logged and shown).
    Q_INVOKABLE void error(const QString& text);
    Q_INVOKABLE void dismiss(int id);

    // Tests: how long a toast stays.
    void setShowMs(int ms) { show_ms_ = ms; }

signals:
    void toastsChanged();

private:
    void show(const QString& text);
    struct Toast {
        int id;
        QString text;
    };
    QList<Toast> toasts_;
    int next_id_ = 1;
    int show_ms_ = kShowMs;
};

}  // namespace samn::ui
