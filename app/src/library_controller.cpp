#include "library_controller.h"

#include <QFileInfo>

#include "samn/library.h"

namespace samn::ui {

namespace {
std::string u8(const QString& s) { return s.toUtf8().toStdString(); }
QString q(const std::string& s) { return QString::fromUtf8(s.c_str(), static_cast<int>(s.size())); }
}  // namespace

LibraryController::LibraryController(QObject* parent) : QObject(parent) {}
LibraryController::~LibraryController() = default;

samn::Settings& LibraryController::settings() const {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

QStringList LibraryController::list(const char* key) const {
    QStringList out;
    try {
        const auto v = settings().get(key, nlohmann::json::array());
        if (v.is_array())
            for (const auto& e : v)
                if (e.is_string() && !e.get<std::string>().empty()) out << q(e.get<std::string>());
    } catch (const std::exception&) {
    }
    return out;
}

void LibraryController::store(const char* key, const QStringList& list) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : list) arr.push_back(u8(p));
    try {
        settings().set(key, arr);
    } catch (const std::exception&) {
        // The Library is a convenience; a read-only config must not break playing.
    }
    emit changed();
}

void LibraryController::push(const char* key, const QString& path, size_t max) {
    if (path.isEmpty()) return;
    std::vector<std::string> v;
    for (const auto& p : list(key)) v.push_back(u8(p));
    v = samn::PushFront(std::move(v), u8(QFileInfo(path).absoluteFilePath()), max);
    QStringList out;
    for (const auto& p : v) out << q(p);
    store(key, out);
}

void LibraryController::noteScriptOpened(const QString& path) {
    push(samn::kRecentKey, path, samn::kRecentMax);
    push(samn::kImportedKey, path, samn::kScriptsMax);
}

void LibraryController::noteVideoOpened(const QString& path) { push(samn::kRecentVideosKey, path, samn::kRecentMax); }
void LibraryController::noteScriptEdited(const QString& path) { push(samn::kEditedKey, path, samn::kScriptsMax); }
void LibraryController::noteScriptGenerated(const QString& path) { push(samn::kGeneratedKey, path, samn::kScriptsMax); }

bool LibraryController::toggleFavorite(const QString& path) {
    std::vector<std::string> v;
    for (const auto& p : list(samn::kFavoritesKey)) v.push_back(u8(p));
    bool on = false;
    v = samn::ToggleFavorite(std::move(v), u8(QFileInfo(path).absoluteFilePath()), &on);
    QStringList out;
    for (const auto& p : v) out << q(p);
    store(samn::kFavoritesKey, out);
    return on;
}

bool LibraryController::isFavorite(const QString& path) const {
    return !path.isEmpty() && list(samn::kFavoritesKey).contains(QFileInfo(path).absoluteFilePath());
}

QVariantList LibraryController::entries(const char* key) const {
    QVariantList out;
    for (const auto& p : list(key)) {
        const QFileInfo fi(p);
        out.append(QVariantMap{{"path", p}, {"name", fi.fileName()}, {"exists", fi.exists()}});
    }
    return out;
}

}  // namespace samn::ui
