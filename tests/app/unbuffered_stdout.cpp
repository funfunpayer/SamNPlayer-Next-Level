// Linked into every app test. A piped stdout is fully buffered on Windows,
// so a test that crashed (or crashed at exit) left no output at all in CI.
// Unbuffered, the log shows how far it got and which function failed.
#include <cstdio>

namespace {
const int g_unbuffered = [] {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    return 0;
}();
}  // namespace
