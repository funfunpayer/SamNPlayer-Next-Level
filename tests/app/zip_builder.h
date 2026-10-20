#pragma once
// Zip archives for the tests, built in memory the way Python's wheel builder
// writes them (stored or deflated entries, no ZIP64).
#include <QByteArray>
#include <QList>
#include <zlib.h>

namespace testzip {
inline void Put16(QByteArray& b, quint16 v) {
    b.append(char(v & 0xff));
    b.append(char(v >> 8));
}
inline void Put32(QByteArray& b, quint32 v) {
    for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xff));
}
inline QByteArray Deflate(const QByteArray& data) {
    z_stream s{};
    deflateInit2(&s, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(int(deflateBound(&s, uLong(data.size()))), Qt::Uninitialized);
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    s.avail_in = uInt(data.size());
    s.next_out = reinterpret_cast<Bytef*>(out.data());
    s.avail_out = uInt(out.size());
    deflate(&s, Z_FINISH);
    out.resize(qsizetype(s.total_out));
    deflateEnd(&s);
    return out;
}
struct File {
    QByteArray name, data;
    bool deflated;
};
// A zip archive as Python's wheel builder writes one.
inline QByteArray Zip(const QList<File>& files) {
    QByteArray zip, dir;
    for (const File& f : files) {
        const quint32 crc = crc32(0L, reinterpret_cast<const Bytef*>(f.data.constData()), uInt(f.data.size()));
        const QByteArray packed = f.deflated ? Deflate(f.data) : f.data;
        const quint32 offset = quint32(zip.size());
        Put32(zip, 0x04034b50);
        Put16(zip, 20); Put16(zip, 0); Put16(zip, f.deflated ? 8 : 0); Put16(zip, 0); Put16(zip, 0);
        Put32(zip, crc); Put32(zip, quint32(packed.size())); Put32(zip, quint32(f.data.size()));
        Put16(zip, quint16(f.name.size())); Put16(zip, 0);
        zip += f.name + packed;
        Put32(dir, 0x02014b50);
        Put16(dir, 20); Put16(dir, 20); Put16(dir, 0); Put16(dir, f.deflated ? 8 : 0); Put16(dir, 0); Put16(dir, 0);
        Put32(dir, crc); Put32(dir, quint32(packed.size())); Put32(dir, quint32(f.data.size()));
        Put16(dir, quint16(f.name.size())); Put16(dir, 0); Put16(dir, 0); Put16(dir, 0); Put16(dir, 0);
        Put32(dir, 0); Put32(dir, offset);
        dir += f.name;
    }
    const quint32 dir_offset = quint32(zip.size());
    zip += dir;
    Put32(zip, 0x06054b50);
    Put16(zip, 0); Put16(zip, 0); Put16(zip, quint16(files.size())); Put16(zip, quint16(files.size()));
    Put32(zip, quint32(dir.size())); Put32(zip, dir_offset); Put16(zip, 0);
    return zip;
}
}  // namespace testzip
