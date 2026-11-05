#pragma once

// Switches the numeric part of the C locale to a comma-decimal one for a test
// (Qt on Unix calls setlocale(LC_ALL, "") at start-up, so a German or French
// desktop reads "1,6", not "1.6"). Not every machine has such a locale
// installed: glibc is asked to build one with localedef when it is missing.

#include <doctest/doctest.h>

#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace samn::testing {

class CommaLocale {
public:
    CommaLocale() {
        static const char* const names[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "de-DE", "German_Germany.1252",
                                            "fr_FR.UTF-8",  "fr_FR.utf8", "fr_FR"};
        for (const char* n : names)
            if (Try(n)) return;
#ifndef _WIN32
        // glibc: build de_DE into a scratch directory and point LOCPATH at it.
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "samn_comma_locale";
        std::filesystem::create_directories(dir);
        const std::string cmd =
            "localedef -i de_DE -f UTF-8 '" + (dir / "de_DE.UTF-8").string() + "' >/dev/null 2>&1";
        if (std::system(cmd.c_str()) >= 0) {
            setenv("LOCPATH", dir.c_str(), 1);
            if (Try("de_DE.UTF-8")) return;
        }
#endif
    }
    ~CommaLocale() {
        if (ok_) std::setlocale(LC_NUMERIC, "C");
    }
    CommaLocale(const CommaLocale&) = delete;
    CommaLocale& operator=(const CommaLocale&) = delete;
    bool ok() const { return ok_; }

private:
    bool Try(const char* name) {
        if (!std::setlocale(LC_NUMERIC, name)) return false;
        const std::lconv* lc = std::localeconv();
        ok_ = lc && lc->decimal_point && lc->decimal_point[0] == ',';
        if (!ok_) std::setlocale(LC_NUMERIC, "C");
        return ok_;
    }
    bool ok_ = false;
};

}  // namespace samn::testing

// Without a comma locale a developer machine skips the test with a message;
// CI must have one (a skipped test there would hide the bug it guards).
#define SAMN_REQUIRE_COMMA_LOCALE(loc)                                                                   \
    do {                                                                                                 \
        if (!(loc).ok()) {                                                                               \
            if (std::getenv("CI")) FAIL("CI needs a comma-decimal locale (apt-get install locales)");    \
            MESSAGE("SKIPPED: no comma-decimal locale on this machine (set LOCPATH or install de_DE)"); \
            return;                                                                                      \
        }                                                                                                \
    } while (0)
