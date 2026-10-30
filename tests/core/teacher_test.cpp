// Teachers on a model server (D-155 step 4): request, answer, boxes.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "samn/teacher.h"

TEST_CASE("teacher: the request carries the prompt and the frame") {
    const auto prompt = samn::TeacherPrompt({"hand", "mouth"});
    CHECK(prompt.find("hand, mouth") != std::string::npos);
    const auto body = nlohmann::json::parse(samn::TeacherRequestBody("qwen2.5vl", prompt, "QUJD"));
    CHECK(body["model"] == "qwen2.5vl");
    CHECK(body["stream"] == false);
    const auto& content = body["messages"][0]["content"];
    CHECK(content[0]["text"] == prompt);
    CHECK(content[1]["image_url"]["url"] == "data:image/jpeg;base64,QUJD");
}

TEST_CASE("teacher: answers, errors and the model list") {
    std::string error;
    CHECK(samn::ChatContent(R"({"choices":[{"message":{"role":"assistant","content":"hi"}}]})").value() == "hi");
    CHECK_FALSE(samn::ChatContent(R"({"error":{"message":"model not found"}})", &error));
    CHECK(error == "model not found");
    CHECK_FALSE(samn::ChatContent("<html>", &error));
    CHECK_FALSE(samn::ChatContent(R"({"choices":[]})", &error));
    CHECK(samn::ParseModelList(R"({"data":[{"id":"llava"},{"id":"gemma3"}]})") ==
          std::vector<std::string>{"gemma3", "llava"});
    CHECK(samn::ParseModelList("nonsense").empty());
}

// A server error whose message is not text is still an error, never a throw.
TEST_CASE("teacher: an error message that is not text") {
    std::string error;
    std::optional<std::string> got;
    CHECK_NOTHROW(got = samn::ChatContent(R"({"error":{"message":null}})", &error));
    CHECK_FALSE(got);
    CHECK(error == "server error");
    CHECK_NOTHROW(got = samn::ChatContent(R"({"error":{"message":{"text":"x"},"code":7}})", &error));
    CHECK_FALSE(got);
    CHECK(error == "server error");
}

TEST_CASE("teacher: boxes as fractions, pixels or 0..1000, in prose or a code fence") {
    const std::vector<std::string> classes{"hand", "mouth"};
    // Fractions, in a code fence after some prose; "hand 2" is a hand; a face is not asked.
    auto b = samn::ParseTeacherBoxes(
        "Sure!\n```json\n{\"objects\": [{\"label\": \"Hand\", \"box\": [0.1, 0.2, 0.25, 0.5]},"
        " {\"label\": \"hand 2\", \"box\": [0.5, 0.5, 0.1, 0.1]}, {\"label\": \"face\", \"box\": [0, 0, 0.5, 0.5]}]}\n```",
        640, 360, classes);
    REQUIRE(b.size() == 2);
    CHECK(b[0].cls == "hand");
    CHECK(b[0].box.x == doctest::Approx(64));
    CHECK(b[0].box.h == doctest::Approx(180));
    // Corners in pixels.
    b = samn::ParseTeacherBoxes(R"([{"class":"mouth","bbox_2d":[100,50,200,150]}])", 640, 360, classes);
    REQUIRE(b.size() == 1);
    CHECK(b[0].box.w == doctest::Approx(100));
    // Corners on 0..1000 (larger than the picture): scaled.
    b = samn::ParseTeacherBoxes(R"({"objects":[{"label":"mouth","bbox":[500,500,1000,900]}]})", 640, 360, classes);
    REQUIRE(b.size() == 1);
    CHECK(b[0].box.x == doctest::Approx(320));
    CHECK(b[0].box.h == doctest::Approx(144));
    // Nothing, broken JSON and empty boxes give nothing.
    CHECK(samn::ParseTeacherBoxes(R"({"objects": []})", 640, 360, classes).empty());
    CHECK(samn::ParseTeacherBoxes("I cannot see anything {", 640, 360, classes).empty());
    CHECK(samn::ParseTeacherBoxes(R"({"objects":[{"label":"hand","box":[0.5,0.5,0,0]}]})", 640, 360, classes).empty());
}

TEST_CASE("teacher: proposals go to the nearest object or a new one, never over a confirmed key") {
    samn::LabelProject p;
    p.duration_ms = 10000;
    samn::LabelKey k;
    k.ms = 0;
    k.box = {10, 10, 50, 50};
    k.confirmed = true;
    p.AddObject("hand").SetKey(k);
    const std::vector<samn::TeacherBox> boxes{{"hand", {12, 12, 50, 50}}, {"hand", {300, 10, 40, 40}},
                                              {"mouth", {200, 200, 30, 20}}};
    // At 0 the user's hand stays; a second hand and a mouth are new.
    CHECK(samn::ApplyTeacherBoxes(p, 0, boxes, "teacher:test") == 2);
    CHECK(p.ObjectCount("hand") == 2);
    CHECK(p.Track("hand")->KeyAt(0)->confirmed);
    CHECK(p.Track("hand 2")->KeyAt(0)->source == "teacher:test");
    CHECK_FALSE(p.Track("mouth")->KeyAt(0)->confirmed);
    // Later: the boxes go to the objects they overlap, nothing new.
    CHECK(samn::ApplyTeacherBoxes(p, 5000, boxes, "teacher:test") == 3);
    CHECK(p.ObjectCount("hand") == 2);
    CHECK(p.Track("hand")->KeyAt(5000)->box.x == doctest::Approx(12));
    CHECK(p.Track("hand 2")->KeyAt(5000)->box.x == doctest::Approx(300));
}
