#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace samn::ui {

// "Check for updates" (D-147: check only, 0.7 Settings → Runtime & updates
// and the startup banner): asks GitHub for the newest release and offers its
// page. One instance for the app (Main.qml); the Settings page and the
// startup banner bind to it. SAMN_UPDATE_URL replaces the API address
// (tests: a local file).
class UpdateChecker : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0.7 "update.check_on_startup" (default on).
    Q_PROPERTY(bool checkOnStartup READ checkOnStartup WRITE setCheckOnStartup NOTIFY checkOnStartupChanged)
    // idle | checking | available | current | error
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString latestTag READ latestTag NOTIFY changed)
    Q_PROPERTY(QString notesPreview READ notesPreview NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    // A startup check found an update the banner has not been closed for.
    Q_PROPERTY(bool bannerVisible READ bannerVisible NOTIFY changed)
    // The support note (D-153): a link bottom left, and a card once at start
    // while "ui.support_note_at_start" is on (default on).
    Q_PROPERTY(bool supportNoteAtStart READ supportNoteAtStart WRITE setSupportNoteAtStart NOTIFY supportNoteAtStartChanged)
    Q_PROPERTY(QString projectPageUrl READ projectPageUrl CONSTANT)

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    bool checkOnStartup() const { return check_on_startup_; }
    void setCheckOnStartup(bool on);
    QString state() const { return state_; }
    QString statusText() const;
    QString latestTag() const { return latest_tag_; }
    QString notesPreview() const { return notes_preview_; }
    QString currentVersion() const;
    bool bannerVisible() const { return banner_ && state_ == QLatin1String("available"); }
    bool supportNoteAtStart() const { return support_at_start_; }
    void setSupportNoteAtStart(bool on);
    QString projectPageUrl() const;

    // `onStartup`: quiet — a failure goes to the log only and a found update
    // shows the banner (0.7 main.js).
    Q_INVOKABLE void check(bool onStartup = false);
    // Starts the startup check when the setting is on.
    Q_INVOKABLE void checkIfEnabled() { if (check_on_startup_) check(true); }
    // Opens the release page in the browser (only this project's pages).
    Q_INVOKABLE bool openReleasePage();
    Q_INVOKABLE void dismissBanner();
    // The project's GitHub page (the support note).
    Q_INVOKABLE bool openProjectPage();

signals:
    void checkOnStartupChanged();
    void supportNoteAtStartChanged();
    void changed();

private:
    void finish(QNetworkReply* reply);
    void setResult(const QString& state, const QString& error = QString());

    QNetworkAccessManager* net_ = nullptr;
    QPointer<QNetworkReply> reply_;
    bool check_on_startup_ = true;
    bool support_at_start_ = true;
    bool quiet_ = false;
    bool banner_ = false;
    QString state_ = QStringLiteral("idle");
    QString error_;
    QString latest_tag_;
    QString notes_preview_;
    QString page_url_;
};

}  // namespace samn::ui
