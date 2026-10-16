#include "samn/library.h"

#include <algorithm>

namespace samn {

std::vector<std::string> PushFront(std::vector<std::string> list, const std::string& path, size_t max) {
    if (path.empty()) return list;
    list.erase(std::remove(list.begin(), list.end(), path), list.end());
    list.insert(list.begin(), path);
    if (list.size() > max) list.resize(max);
    return list;
}

std::vector<std::string> ToggleFavorite(std::vector<std::string> list, const std::string& path, bool* on) {
    const bool present = std::find(list.begin(), list.end(), path) != list.end();
    if (on) *on = !present && !path.empty();
    if (path.empty()) return list;
    if (present) {
        list.erase(std::remove(list.begin(), list.end(), path), list.end());
        return list;
    }
    return PushFront(std::move(list), path, kFavoritesMax);
}

}  // namespace samn
