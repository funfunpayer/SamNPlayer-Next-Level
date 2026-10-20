#pragma once
// Models fetched with one click (D-155 step 4): NudeNet comes inside its
// PyPI wheel, a zip archive. The archive and the model in it are both
// checked against pinned SHA-256 hashes before anything is written.

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

namespace samn::ui {

// One file out of a zip archive (stored or deflated, no ZIP64), its CRC
// checked. nullopt with *error set when the name is missing or the archive
// is broken.
std::optional<QByteArray> ReadZipEntry(const QByteArray& zip, const QString& name, QString* error);

struct ModelSource {
    QString name;               // shown to the user
    QUrl url;                   // the archive
    QByteArray archive_sha256;  // hex
    QString entry;              // the model's path inside it
    QByteArray model_sha256;    // hex
    QString file_name;          // under config_dir()/models
};

// NudeNet 3.4.2's 320n detector from PyPI. Its weights are not bundled
// (their licence is unclear, D-155); the user fetches them.
ModelSource NudeNetSource();

// The model out of a downloaded archive, both hashes checked.
std::optional<QByteArray> ExtractModel(const QByteArray& archive, const ModelSource& source, QString* error);

}  // namespace samn::ui
