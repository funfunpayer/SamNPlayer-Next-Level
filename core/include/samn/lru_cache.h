#pragma once
// A cache with a hard size limit (D-142): when an insert goes over the
// budget, the least recently used entries are dropped first. No garbage
// collector; the budget is the only rule. Not thread-safe: the owner locks.
//
// cost(value) gives an entry's size in the budget's unit (bytes for images).
// An entry larger than the whole budget is not stored.
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace samn {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    using CostFn = std::function<size_t(const Value&)>;

    LruCache(size_t budget, CostFn cost) : budget_(budget), cost_(std::move(cost)) {}

    // The value, now the most recently used; nullopt when not cached.
    [[nodiscard]] std::optional<Value> Get(const Key& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) {
            ++misses_;
            return std::nullopt;
        }
        ++hits_;
        order_.splice(order_.begin(), order_, it->second);
        return it->second->value;
    }

    [[nodiscard]] bool Contains(const Key& key) const { return index_.count(key) != 0; }

    // Stores (or replaces) the value; returns false when it alone is larger
    // than the budget.
    bool Put(const Key& key, Value value) {
        const size_t cost = cost_(value);
        Remove(key);
        if (cost > budget_) return false;
        order_.push_front(Entry{key, std::move(value), cost});
        index_.emplace(key, order_.begin());
        used_ += cost;
        while (used_ > budget_) {
            const Entry& last = order_.back();
            used_ -= last.cost;
            index_.erase(last.key);
            order_.pop_back();
            ++evictions_;
        }
        return true;
    }

    void Remove(const Key& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) return;
        used_ -= it->second->cost;
        order_.erase(it->second);
        index_.erase(it);
    }

    void Clear() {
        order_.clear();
        index_.clear();
        used_ = 0;
    }

    // Shrinks or grows the budget; shrinking drops the oldest entries now.
    void SetBudget(size_t budget) {
        budget_ = budget;
        while (used_ > budget_ && !order_.empty()) {
            used_ -= order_.back().cost;
            index_.erase(order_.back().key);
            order_.pop_back();
            ++evictions_;
        }
    }

    [[nodiscard]] size_t used() const { return used_; }
    [[nodiscard]] size_t budget() const { return budget_; }
    [[nodiscard]] size_t size() const { return index_.size(); }
    [[nodiscard]] size_t hits() const { return hits_; }
    [[nodiscard]] size_t misses() const { return misses_; }
    [[nodiscard]] size_t evictions() const { return evictions_; }

private:
    struct Entry {
        Key key;
        Value value;
        size_t cost;
    };
    size_t budget_;
    CostFn cost_;
    std::list<Entry> order_;  // most recently used first
    std::unordered_map<Key, typename std::list<Entry>::iterator, Hash> index_;
    size_t used_ = 0;
    size_t hits_ = 0, misses_ = 0, evictions_ = 0;
};

}  // namespace samn
