#include "samn/ringdown.h"

namespace samn {

std::vector<Action> RingDown(std::vector<Action> actions, int64_t at_ms, int last_pos, int cycles) {
    if (cycles < 1) cycles = 1;
    if (cycles > 2) cycles = 2;
    int pos = last_pos;
    if (pos < 0) pos = 0;
    if (pos > 100) pos = 100;
    // Odd steps are the low half of the swing (a third of that step's
    // amplitude); even steps are the high half, shrinking each time.
    constexpr int64_t kStepMs = 180;
    const int half = cycles * 2;
    for (int i = 1; i <= half; ++i) {
        int amp = pos * (half - i + 1) / (half + 1);
        if (i % 2 == 1) amp /= 3;
        actions.push_back(Action{at_ms + int64_t(i) * kStepMs, amp});
    }
    actions.push_back(Action{at_ms + int64_t(half + 1) * kStepMs, 0});
    return actions;
}

}  // namespace samn
