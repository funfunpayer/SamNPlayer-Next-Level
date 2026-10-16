#include <doctest/doctest.h>

#include <cmath>

#include "samn/audio_envelope.h"

// Ported from 0.7 generator/audio_vibration_test.go.

TEST_CASE("audio envelope: window RMS with a half-full tail") {
    samn::WindowRms w(2);
    const float s[] = {3, 4, 0, 0, 1};
    w.Push(s, 5);
    const auto got = w.Finish();
    REQUIRE(got.size() == 3);
    CHECK(got[0] == doctest::Approx(std::sqrt(12.5)));
    CHECK(got[1] == 0);
    CHECK(got[2] == doctest::Approx(1));
    // Less than half full: dropped.
    samn::WindowRms w3(3);
    w3.Push(s, 4);
    CHECK(w3.Finish().size() == 1);
}

TEST_CASE("audio envelope: silence, loud and release") {
    std::vector<double> rms;
    for (int i = 0; i < 20; ++i) rms.push_back(0);
    for (int i = 0; i < 20; ++i) rms.push_back(0.5);
    for (int i = 0; i < 20; ++i) rms.push_back(0.005);
    const auto out = samn::NormalizeLoudness(rms);
    CHECK(out[0] == 0);
    CHECK(out[19] == 0);
    CHECK(out[20] >= 0.95);                         // fast attack
    CHECK((out[40] > 0 && out[40] < out[39]));      // release falls off smoothly
    CHECK(out[59] == 0);                            // long quiet tail gates to 0
    for (double v : out) CHECK((v >= 0 && v <= 1));
}

TEST_CASE("audio envelope: an all-silent track stays 0") {
    for (double v : samn::NormalizeLoudness({0, 1e-6, 0})) CHECK(v == 0);
    CHECK(samn::NormalizeLoudness({}).empty());
}
