#include <doctest/doctest.h>

#include "samn/text.h"

using samn::FoldCase;

TEST_CASE("text: case folding beyond ASCII") {
    CHECK(FoldCase("Clip ABC") == "clip abc");
    CHECK(FoldCase("Übung ÄÖÜ") == "übung äöü");
    CHECK(FoldCase("ß") == "ß");
    CHECK(FoldCase("ÉCOLE Ñandú") == "école ñandú");
    CHECK(FoldCase("ŁÓDŹ ŠKODA") == "łódź škoda");
    CHECK(FoldCase("ΑΘΗΝΑ") == "αθηνα");
    CHECK(FoldCase("МОСКВА Ёж") == "москва ёж");
    CHECK(FoldCase("ＡＢＣ") == "ａｂｃ");
    CHECK(FoldCase("İ") == "i");
    CHECK(FoldCase("×÷") == "×÷");
    // Emoji and CJK stay; invalid bytes pass through.
    CHECK(FoldCase("😀 東京") == "😀 東京");
    CHECK(FoldCase(std::string("A\xFF" "B\xC3")) == std::string("a\xFF" "b\xC3"));
}
