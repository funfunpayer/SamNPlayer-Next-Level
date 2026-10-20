#include "samn/polarity.h"

#include <cstdio>

namespace samn {

PolarityHint SuggestPolarity(const std::vector<Action>& actions) {
    PolarityHint h;
    if (actions.size() < 4) {
        h.reason = "too few points to estimate the direction";
        return h;
    }
    const int64_t start = actions.front().at, end = actions.back().at;
    if (end <= start) {
        h.reason = "the script has no duration";
        return h;
    }
    const int64_t mid = start + (end - start) / 2;
    double sum = 0;
    int n = 0;
    for (const Action& a : actions)
        if (a.at <= mid) {
            sum += a.pos;
            ++n;
        }
    if (n == 0) {
        h.reason = "no points in the first half";
        return h;
    }
    h.first_half_mean = sum / n;
    char buf[160];
    if (h.first_half_mean < 40) {
        h.suggest_invert = true;
        std::snprintf(buf, sizeof buf, "the first half sits at a mean of %.0f — invert if 100 should be \"in/up\"",
                      h.first_half_mean);
    } else if (h.first_half_mean > 60) {
        std::snprintf(buf, sizeof buf, "the first half sits at a mean of %.0f — the direction looks consistent",
                      h.first_half_mean);
    } else {
        std::snprintf(buf, sizeof buf, "the first half sits at a mean of %.0f — no clear direction, nothing to suggest",
                      h.first_half_mean);
    }
    h.reason = buf;
    return h;
}

}  // namespace samn
