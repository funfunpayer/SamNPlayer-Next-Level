#pragma once
// The Player's play list (0.7 playback.js playlist): an ordered list of
// scripts with the active one, Auto-advance, Shuffle and Repeat. Pure logic;
// the app loads the scripts. Randomness comes from a caller-provided engine
// so tests are repeatable.
#include <cstddef>
#include <random>
#include <string>
#include <vector>

namespace samn {

class Playlist {
public:
    struct Item {
        std::string path;
        std::string name;  // file name without the extension
    };

    // New list from paths (empty and duplicate paths dropped), `start`
    // clamped (0.7 replacePlaylist).
    void Replace(const std::vector<std::string>& paths, size_t start = 0);
    // Appends a path; when the list is empty, `current` (the loaded script)
    // comes first. false when the path is already in the list (0.7
    // appendToPlaylist: "Already in list").
    bool Append(const std::string& path, const std::string& current = {});
    // Removes item i. true when it was the active one (the caller loads the
    // new active item, if any). The active index follows as in 0.7.
    bool Remove(size_t i);
    // Makes item i active; false when out of range or already active.
    bool Pick(size_t i);

    // Where Next goes: the following item, or with Repeat the first after
    // the last (reshuffled when Shuffle is on). false at the end without
    // Repeat, or with fewer than 2 items.
    bool Advance(std::mt19937& rng);
    // After a video ends: advance only with Auto-advance and somewhere to go
    // (0.7 onPlaybackFinished).
    bool AdvanceOnFinish(std::mt19937& rng);
    // "Next" visible: 2+ items and not at the end, unless Repeat.
    bool HasNext() const;

    // Shuffle on: the active item first, the rest in random order (0.7
    // reshufflePlaylistKeepingCurrent). Off keeps the current order.
    void SetShuffle(bool on, std::mt19937& rng);
    void SetRepeat(bool on) { repeat_ = on; }
    void SetAuto(bool on) { auto_ = on; }

    const std::vector<Item>& Items() const { return items_; }
    size_t Index() const { return index_; }
    const Item* Current() const { return items_.empty() ? nullptr : &items_[index_]; }
    bool Shuffle() const { return shuffle_; }
    bool Repeat() const { return repeat_; }
    bool Auto() const { return auto_; }
    // 0.7 shows the list only with 2 or more items.
    bool Visible() const { return items_.size() >= 2; }

    static std::string NameOf(const std::string& path);

private:
    std::vector<Item> items_;
    size_t index_ = 0;
    bool shuffle_ = false, repeat_ = false, auto_ = true;
};

}  // namespace samn
