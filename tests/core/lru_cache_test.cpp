#include <doctest/doctest.h>

#include <random>
#include <string>

#include "samn/lru_cache.h"

using samn::LruCache;

namespace {
size_t Len(const std::string& s) { return s.size(); }
}  // namespace

TEST_CASE("lru: the least recently used goes first when the budget is full") {
    LruCache<int, std::string> c(10, Len);
    CHECK(c.Put(1, "aaaa"));
    CHECK(c.Put(2, "bbbb"));
    CHECK(c.used() == 8);
    CHECK(c.Get(1) == std::optional<std::string>("aaaa"));  // 1 is now the newest
    CHECK(c.Put(3, "cccc"));                                 // 12 > 10: 2 goes
    CHECK(!c.Contains(2));
    CHECK(c.Contains(1));
    CHECK(c.Contains(3));
    CHECK(c.used() == 8);
    CHECK(c.evictions() == 1);
    CHECK(!c.Get(2));
    CHECK(c.hits() == 1);
    CHECK(c.misses() == 1);
}

TEST_CASE("lru: replace, remove, too large, shrink") {
    LruCache<int, std::string> c(10, Len);
    c.Put(1, "aa");
    c.Put(1, "aaaaaa");
    CHECK(c.size() == 1);
    CHECK(c.used() == 6);
    CHECK(!c.Put(2, std::string(11, 'x')));  // larger than the whole budget
    CHECK(c.size() == 1);
    c.Put(2, "bbb");
    c.SetBudget(4);
    CHECK(c.size() == 1);
    CHECK(c.Contains(2));  // the newest stays
    c.Remove(2);
    CHECK(c.used() == 0);
    c.Put(5, "x");
    c.Clear();
    CHECK(c.size() == 0);
    CHECK(c.used() == 0);
}

TEST_CASE("lru: the budget always holds") {
    std::mt19937 rng(3);
    LruCache<int, std::string> c(1000, Len);
    for (int i = 0; i < 5000; ++i) {
        if (rng() % 3)
            c.Put(int(rng() % 300), std::string(rng() % 120, 'x'));
        else
            (void)c.Get(int(rng() % 300));
        REQUIRE(c.used() <= c.budget());
    }
    size_t sum = 0;
    for (int k = 0; k < 300; ++k)
        if (auto v = c.Get(k)) sum += v->size();
    CHECK(sum == c.used());
}
