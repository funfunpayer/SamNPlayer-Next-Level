#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "samn/library.h"
#include "samn/settings.h"

namespace samn::ui {

// The Library column of the Player (0.7 player_layout.js): recent scripts,
// favorites, recent videos, and the scripts imported / generated / edited.
// Kept in the settings, so they survive a restart and move with the user's
// config. Entries are {path, name, exists} for QML.
class LibraryController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY changed)
    Q_PROPERTY(QVariantList recentVideos READ recentVideos NOTIFY changed)
    Q_PROPERTY(QVariantList imported READ imported NOTIFY changed)
    Q_PROPERTY(QVariantList generated READ generated NOTIFY changed)
    Q_PROPERTY(QVariantList edited READ edited NOTIFY changed)
public:
    explicit LibraryController(QObject* parent = nullptr);
    ~LibraryController() override;

    // A script was opened (goes to Recent and Imported) / its video linked.
    Q_INVOKABLE void noteScriptOpened(const QString& path);
    Q_INVOKABLE void noteVideoOpened(const QString& path);
    // A script was edited and saved / created by the Generator.
    Q_INVOKABLE void noteScriptEdited(const QString& path);
    Q_INVOKABLE void noteScriptGenerated(const QString& path);
    // Returns true when the path is a favorite now.
    Q_INVOKABLE bool toggleFavorite(const QString& path);
    Q_INVOKABLE bool isFavorite(const QString& path) const;

    QVariantList recent() const { return entries(samn::kRecentKey); }
    QVariantList favorites() const { return entries(samn::kFavoritesKey); }
    QVariantList recentVideos() const { return entries(samn::kRecentVideosKey); }
    QVariantList imported() const { return entries(samn::kImportedKey); }
    QVariantList generated() const { return entries(samn::kGeneratedKey); }
    QVariantList edited() const { return entries(samn::kEditedKey); }

signals:
    void changed();

private:
    QStringList list(const char* key) const;
    void store(const char* key, const QStringList& list);
    void push(const char* key, const QString& path, size_t max);
    QVariantList entries(const char* key) const;
    samn::Settings& settings() const;

    mutable std::unique_ptr<samn::Settings> settings_;
};

}  // namespace samn::ui
