#pragma once

// Conversions between Qt strings and the paths and UTF-8 strings of the core.
// QML hands over plain paths or file: URLs; the core takes std::filesystem
// paths and UTF-8 std::string. Via UTF-16, so the same code is right on Linux
// and Windows (where a narrow path is not UTF-8).

#include <filesystem>
#include <string>

#include <QString>
#include <QUrl>

namespace samn::ui {

// The local path of a file: URL, or the text as it is.
inline QString LocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

inline std::filesystem::path FsPath(const QString& p) { return std::filesystem::path(p.toStdU16String()); }
inline QString FromFs(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }

// The path as UTF-8 text for core functions that take std::string. (path::u8string()
// is a std::u8string from C++20 on and does not convert.)
inline std::string PathUtf8(const std::filesystem::path& p) { return FromFs(p).toUtf8().toStdString(); }

inline std::string U8(const QString& s) { return s.toUtf8().toStdString(); }
inline QString FromU8(const std::string& s) { return QString::fromUtf8(s.c_str(), static_cast<int>(s.size())); }

}  // namespace samn::ui
