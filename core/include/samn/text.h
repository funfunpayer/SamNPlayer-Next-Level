#pragma once

// Text helpers without ICU.

#include <string>

namespace samn {

// Lower case for UTF-8 text, like Go's strings.ToLower for the scripts
// file names use: ASCII, Latin-1, Latin Extended-A, Greek, Cyrillic and full-width Latin. "Übung" and "übung"
// compare equal. Other characters and invalid bytes pass through unchanged.
std::string FoldCase(const std::string& utf8);

// Only A-Z become a-z; every other byte is kept. For protocol words, file
// extensions and JSON keys, which are ASCII. Not for user-visible text: use
// FoldCase there.
std::string AsciiLower(std::string s);

}  // namespace samn
