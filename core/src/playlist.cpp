#include "samn/playlist.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace samn {

std::string Playlist::NameOf(const std::string& path) {
    return std::filesystem::u8path(path).stem().u8string();
}

void Playlist::Replace(const std::vector<std::string>& paths, size_t start) {
    items_.clear();
    std::unordered_set<std::string> seen;
    for (const auto& p : paths) {
        if (p.empty() || !seen.insert(p).second) continue;
        items_.push_back({p, NameOf(p)});
    }
    index_ = items_.empty() ? 0 : std::min(start, items_.size() - 1);
}

bool Playlist::Append(const std::string& path, const std::string& current) {
    if (path.empty()) return false;
    for (const auto& it : items_)
        if (it.path == path) return false;
    if (items_.empty() && !current.empty() && current != path) {
        items_.push_back({current, NameOf(current)});
        index_ = 0;
    }
    items_.push_back({path, NameOf(path)});
    return true;
}

bool Playlist::Remove(size_t i) {
    if (i >= items_.size()) return false;
    const bool removing_current = i == index_;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
    if (items_.empty()) {
        index_ = 0;
        return removing_current;
    }
    if (index_ >= items_.size())
        index_ = items_.size() - 1;
    else if (i < index_)
        --index_;
    return removing_current;
}

bool Playlist::Pick(size_t i) {
    if (i >= items_.size() || i == index_) return false;
    index_ = i;
    return true;
}

bool Playlist::HasNext() const {
    if (items_.size() < 2) return false;
    return index_ < items_.size() - 1 || repeat_;
}

bool Playlist::Advance(std::mt19937& rng) {
    if (!HasNext()) return false;
    if (index_ < items_.size() - 1) {
        ++index_;
        return true;
    }
    // At the end with Repeat: a new cycle, fully reshuffled with Shuffle on.
    if (shuffle_) std::shuffle(items_.begin(), items_.end(), rng);
    index_ = 0;
    return true;
}

bool Playlist::AdvanceOnFinish(std::mt19937& rng) { return auto_ && Advance(rng); }

void Playlist::SetShuffle(bool on, std::mt19937& rng) {
    shuffle_ = on;
    if (!on || items_.size() < 2) return;
    Item cur = items_[index_];
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index_));
    std::shuffle(items_.begin(), items_.end(), rng);
    items_.insert(items_.begin(), std::move(cur));
    index_ = 0;
}

}  // namespace samn
