#include "notices.h"

#include <QTimer>

#include "app_log.h"

namespace samn::ui {

Notices::Notices(QObject* parent) : QObject(parent) {
    connect(&AppLog::instance(), &AppLog::errorReported, this, &Notices::show);
}

QVariantList Notices::toasts() const {
    QVariantList out;
    for (const Toast& t : toasts_) out << QVariantMap{{"id", t.id}, {"text", t.text}};
    return out;
}

void Notices::error(const QString& text) { AppLog::instance().reportError(text); }

void Notices::show(const QString& raw) {
    const QString text = raw.trimmed();
    if (text.isEmpty()) return;
    const QString shown = text.size() > kMaxChars ? text.left(kMaxChars - 3) + QStringLiteral("…") : text;
    // The same failure twice in a row is one toast (a retry, two sources);
    // the toast keeps the shortened text, so that is what is compared.
    if (!toasts_.isEmpty() && toasts_.last().text == shown) return;
    const int id = next_id_++;
    toasts_.append({id, shown});
    while (toasts_.size() > kMax) toasts_.removeFirst();
    QTimer::singleShot(show_ms_, this, [this, id] { dismiss(id); });
    emit toastsChanged();
}

void Notices::dismiss(int id) {
    for (int i = 0; i < toasts_.size(); ++i)
        if (toasts_[i].id == id) {
            toasts_.removeAt(i);
            emit toastsChanged();
            return;
        }
}

}  // namespace samn::ui
