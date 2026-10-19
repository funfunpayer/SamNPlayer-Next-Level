#include "samn/engine/generate_queue.h"

#include <algorithm>
#include <system_error>

#include "samn/engine/tip_find.h"
#include "samn/samn_file.h"

namespace samn::engine {

std::filesystem::path ScriptPathForVideo(const std::filesystem::path& video) {
    std::filesystem::path out = video;
    out.replace_extension(".funscript");
    return out;
}

QueueItemOutcome GenerateQueueItem(const std::filesystem::path& video, const QueueSettings& settings,
                                   const std::function<bool()>& cancel,
                                   const std::function<void(int percent)>& progress) {
    QueueItemOutcome out;
    auto report = [&](int p) {
        if (progress) progress(std::clamp(p, 0, 100));
    };
    const std::filesystem::path script = ScriptPathForVideo(video);
    std::error_code ec;
    if (!settings.overwrite &&
        (std::filesystem::exists(script, ec) ||
         std::filesystem::exists(std::filesystem::u8path(CompanionSamnPath(script.u8string())), ec))) {
        out.status = QueueItemStatus::kSkipped;
        out.message = "a script exists already";
        return out;
    }

    TipFindOptions to;
    to.cancel = cancel;
    to.progress = [&](int p) { report(p / 5); };
    std::string error;
    const auto tip = FindTip(video, to, &error);
    if (!tip) {
        out.status = error == "canceled" ? QueueItemStatus::kCanceled : QueueItemStatus::kFailed;
        out.message = error;
        return out;
    }
    if (tip->candidates.empty() || tip->candidates.front().score < kWeakTipScore) {
        out.status = QueueItemStatus::kFailed;
        out.message = "only weak motion found - mark the tip by hand";
        return out;
    }
    out.box = tip->box;
    report(20);

    GenerateRequest req;
    req.video = video;
    req.box = tip->box;
    req.signal = settings.signal;
    req.camera_compensation = settings.camera_compensation;
    req.scene_cuts = settings.scene_cuts;
    req.auto_ozone = settings.auto_ozone;
    req.overwrite = settings.overwrite;
    req.contact_vibration = settings.contact_vibration;
    req.contact_vibration_span = settings.contact_vibration_span;
    req.contact_vibration_curve = settings.contact_vibration_curve;
    req.tip_class = settings.tip_class;
    req.output = script;
    Generate(req, 1, cancel, [&](const GenerateEvent& e) {
        switch (e.kind) {
            case GenerateEventKind::kProgress:
                if (e.percent >= 0) report(20 + e.percent * 80 / 100);
                break;
            case GenerateEventKind::kDone:
                out.status = QueueItemStatus::kDone;
                out.message = e.message;
                out.output = script;
                break;
            case GenerateEventKind::kCanceled:
                out.status = QueueItemStatus::kCanceled;
                out.message = e.message;
                break;
            case GenerateEventKind::kError:
                out.status = QueueItemStatus::kFailed;
                out.message = e.message;
                break;
            case GenerateEventKind::kNote:
                break;
        }
    });
    if (out.status == QueueItemStatus::kDone) report(100);
    return out;
}

}  // namespace samn::engine
