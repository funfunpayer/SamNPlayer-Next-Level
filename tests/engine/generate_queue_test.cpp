// One video of the Generator's queue: Find tip, then create beside the video.
#include <doctest/doctest.h>

#include <filesystem>

#include "samn/engine/generate_queue.h"
#include "synthetic_clips.h"

namespace fs = std::filesystem;
using samn::engine::GenerateQueueItem;
using samn::engine::QueueItemStatus;
using samn::engine::QueueSettings;
namespace clips = samn::engine::testclips;

namespace {
fs::path Dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
}  // namespace

TEST_CASE("queue item: finds the tip, creates beside the video, skips a second time") {
    const fs::path dir = Dir("samn_queue_item");
    REQUIRE(clips::WriteMoving(dir / "a.clip.mp4", 8, 80.0, 1.0));
    CHECK(samn::engine::ScriptPathForVideo(dir / "a.clip.mp4") == dir / "a.clip.funscript");

    QueueSettings s;
    s.signal.profile = "standard";
    int last = -1;
    bool backwards = false;
    const auto done = GenerateQueueItem(dir / "a.clip.mp4", s, {}, [&](int p) {
        backwards |= p < last;
        last = p;
    });
    REQUIRE_MESSAGE(done.status == QueueItemStatus::kDone, done.message);
    CHECK(done.output == dir / "a.clip.funscript");
    CHECK(fs::exists(done.output));
    CHECK(fs::exists(dir / "a.clip.samn"));
    CHECK(done.box == cv::Rect(116, 96, 86, 48));  // Find tip's 0.7 box for this clip
    CHECK(!backwards);
    CHECK(last == 100);

    const auto again = GenerateQueueItem(dir / "a.clip.mp4", s, {}, {});
    CHECK(again.status == QueueItemStatus::kSkipped);
    s.overwrite = true;
    CHECK(GenerateQueueItem(dir / "a.clip.mp4", s, {}, {}).status == QueueItemStatus::kDone);
    fs::remove_all(dir);
}

TEST_CASE("queue item: no rhythm, missing and cancel are not done") {
    const fs::path dir = Dir("samn_queue_item_bad");
    QueueSettings s;
    REQUIRE(clips::WriteMoving(dir / "still.mp4", 4, 0.0, 1.0));
    const auto still = GenerateQueueItem(dir / "still.mp4", s, {}, {});
    CHECK(still.status == QueueItemStatus::kFailed);
    CHECK(still.message.find("weak") != std::string::npos);
    CHECK(!fs::exists(dir / "still.funscript"));

    CHECK(GenerateQueueItem(dir / "missing.mp4", s, {}, {}).status == QueueItemStatus::kFailed);

    REQUIRE(clips::WriteMoving(dir / "c.mp4", 6, 80.0, 1.0));
    int polls = 0;
    const auto canceled = GenerateQueueItem(dir / "c.mp4", s, [&] { return ++polls > 5; }, {});
    CHECK(canceled.status == QueueItemStatus::kCanceled);
    CHECK(!fs::exists(dir / "c.funscript"));
    fs::remove_all(dir);
}
