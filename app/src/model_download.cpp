#include "model_download.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <zlib.h>

namespace samn::ui {

namespace {

constexpr qint64 kMaxEntryBytes = 512LL * 1024 * 1024;

quint32 Le32(const QByteArray& b, qint64 at) {
    const auto* p = reinterpret_cast<const uchar*>(b.constData() + at);
    return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24;
}
quint16 Le16(const QByteArray& b, qint64 at) {
    const auto* p = reinterpret_cast<const uchar*>(b.constData() + at);
    return quint16(p[0] | p[1] << 8);
}
QString Tr(const char* s) { return QCoreApplication::translate("ModelDownload", s); }

}  // namespace

std::optional<QByteArray> ReadZipEntry(const QByteArray& zip, const QString& name, QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return std::nullopt;
    };
    const qint64 n = zip.size();
    // The end-of-central-directory record, at most 64 KiB of comment after it.
    qint64 eocd = -1;
    for (qint64 at = n - 22; at >= 0 && at >= n - 22 - 65535; --at)
        if (Le32(zip, at) == 0x06054b50) {
            eocd = at;
            break;
        }
    if (eocd < 0) return fail(Tr("not a zip archive"));
    const quint16 entries = Le16(zip, eocd + 10);
    qint64 at = Le32(zip, eocd + 16);
    const QByteArray wanted = name.toUtf8();
    for (int i = 0; i < entries; ++i) {
        if (at + 46 > n || Le32(zip, at) != 0x02014b50) return fail(Tr("the archive's directory is broken"));
        const quint16 method = Le16(zip, at + 10);
        const quint32 crc = Le32(zip, at + 16);
        const quint32 packed = Le32(zip, at + 20), size = Le32(zip, at + 24);
        const quint16 name_len = Le16(zip, at + 28), extra_len = Le16(zip, at + 30), comment_len = Le16(zip, at + 32);
        const qint64 local = Le32(zip, at + 42);
        if (at + 46 + name_len > n) return fail(Tr("the archive's directory is broken"));
        const QByteArray entry_name = zip.mid(at + 46, name_len);
        at += 46 + name_len + extra_len + comment_len;
        if (entry_name != wanted) continue;

        if (packed == 0xFFFFFFFFu || size == 0xFFFFFFFFu || size > kMaxEntryBytes)
            return fail(Tr("the file in the archive is too large"));
        if (local + 30 > n || Le32(zip, local) != 0x04034b50) return fail(Tr("the archive is broken"));
        const qint64 data = local + 30 + Le16(zip, local + 26) + Le16(zip, local + 28);
        if (data + qint64(packed) > n) return fail(Tr("the archive is cut short"));
        QByteArray out;
        if (method == 0) {
            if (packed != size) return fail(Tr("the archive is broken"));
            out = zip.mid(data, size);
        } else if (method == 8) {
            out.resize(qsizetype(size));
            z_stream s{};
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return fail(Tr("cannot unpack the archive"));
            s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(zip.constData() + data));
            s.avail_in = packed;
            s.next_out = reinterpret_cast<Bytef*>(out.data());
            s.avail_out = size;
            const int rc = inflate(&s, Z_FINISH);
            const uLong produced = s.total_out;
            inflateEnd(&s);
            if (rc != Z_STREAM_END || produced != size) return fail(Tr("the file in the archive is damaged"));
        } else {
            return fail(Tr("the archive uses a packing this app does not read"));
        }
        if (crc32(0L, reinterpret_cast<const Bytef*>(out.constData()), uInt(out.size())) != crc)
            return fail(Tr("the file in the archive is damaged"));
        return out;
    }
    return fail(Tr("%1 is not in the archive").arg(name));
}

ModelSource NudeNetSource() {
    return {QStringLiteral("NudeNet 320n"),
            QUrl(QStringLiteral("https://files.pythonhosted.org/packages/1c/ee/1aa02d44ba958cc77e16ff1e41a0aac5e721037db7bf"
                                "62b9c9d124917f87/nudenet-3.4.2-py3-none-any.whl")),
            "5937dbd84e5d8e5de038f08ffea5a1bb50a08475776bf2b4795914ce0eaf0331",
            QStringLiteral("nudenet/320n.onnx"),
            "c15d8273adad2d0a92f014cc69ab2d6c311a06777a55545f2c4eb46f51911f0f",
            QStringLiteral("nudenet-320n.onnx")};
}

std::optional<QByteArray> ExtractModel(const QByteArray& archive, const ModelSource& source, QString* error) {
    if (QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex() != source.archive_sha256) {
        if (error) *error = Tr("the download is not the expected file (its checksum differs)");
        return std::nullopt;
    }
    auto model = ReadZipEntry(archive, source.entry, error);
    if (!model) return std::nullopt;
    if (QCryptographicHash::hash(*model, QCryptographicHash::Sha256).toHex() != source.model_sha256) {
        if (error) *error = Tr("the model is not the expected file (its checksum differs)");
        return std::nullopt;
    }
    return model;
}

}  // namespace samn::ui
