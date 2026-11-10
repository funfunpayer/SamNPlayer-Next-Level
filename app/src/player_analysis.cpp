// Script analysis line for the Playback card (0.7 describeScript).
// Automatic motion chapters label that line. Opening a script never writes
// them; applyAutoChapters writes the same labels via SaveScriptChapters.
// A script that already has chapters shows those names instead.
#include "player_controller.h"

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>

#include "qt_paths.h"
#include "samn/script_analysis.h"

namespace samn::ui {
namespace {

std::vector<samn::MotionPoint> PointsOf(const std::vector<samn::Action>& stroke) {
    std::vector<samn::MotionPoint> pts;
    pts.reserve(stroke.size());
    for (const auto& a : stroke)
        pts.push_back(samn::MotionPoint{static_cast<double>(a.at), static_cast<double>(a.pos)});
    return pts;
}

std::vector<samn::MotionChapter> AutoFromStroke(const std::vector<samn::Action>& stroke) {
    if (stroke.size() < 4) return {};
    return samn::MotionChaptersFromSegments(samn::ClassifyMotion(PointsOf(stroke), {}));
}

// 0.7 ScriptChapters. Stored marks win. ClassifyOptions{} leaves smoothing
// off, which is what that call site passes (AnalyzeScript is the one that
// smooths, and only for the summary sentence).
std::vector<samn::MotionChapter> ChaptersFor(const std::vector<samn::Action>& stroke,
                                             const std::vector<samn::Chapter>& stored) {
    if (stroke.size() < 4) return {};
    if (!stored.empty()) {
        std::vector<samn::MotionChapter> out;
        out.reserve(stored.size());
        for (const auto& c : stored) {
            samn::MotionChapter ch;
            ch.kind = c.name.empty() ? "chapter" : c.name;
            ch.start_ms = static_cast<double>(c.start_ms);
            const double end = static_cast<double>(c.end_ms);
            ch.end_ms = end <= ch.start_ms ? ch.start_ms + 1.0 : end;
            out.push_back(std::move(ch));
        }
        return out;
    }
    return AutoFromStroke(stroke);
}

QString Describe(const samn::ScriptAnalysis& analysis, const std::vector<samn::MotionChapter>& chapters) {
    QString text = FromU8(analysis.summary);
    if (chapters.empty()) return text;
    QStringList parts;
    parts.reserve(static_cast<int>(chapters.size()));
    for (const auto& c : chapters) {
        parts << FromU8(samn::MotionChapterLabel(c.kind)) + QLatin1Char(' ') +
                     FromU8(samn::FormatAnalysisClock(c.start_ms)) + QChar(0x2013) +
                     FromU8(samn::FormatAnalysisClock(c.end_ms));
    }
    const QString clause = parts.join(QStringLiteral(", "));
    if (text.isEmpty()) return QStringLiteral("Chapters: ") + clause;
    return text + QStringLiteral(" \u00b7 Chapters: ") + clause;
}

}  // namespace

void PlayerController::refreshScriptAnalysis() {
    QString next;
    if (stroke_.size() >= 4) {
        const samn::ScriptAnalysis analysis = samn::AnalyzeScriptActions(stroke_);
        if (analysis.error.empty()) next = Describe(analysis, ChaptersFor(stroke_, marks_.chapters));
    }
    if (next == scriptAnalysisSummary_) return;
    scriptAnalysisSummary_ = std::move(next);
    emit scriptAnalysisSummaryChanged();
}

QVariantMap PlayerController::analyzeScript() {
    if (stroke_.size() < 4)
        return {{QStringLiteral("error"), QStringLiteral("no script loaded (or too few points)")}};
    const samn::ScriptAnalysis analysis = samn::AnalyzeScriptActions(stroke_);
    if (!analysis.error.empty()) return {{QStringLiteral("error"), FromU8(analysis.error)}};
    QVariantList segments;
    for (const auto& s : analysis.segments) {
        segments << QVariantMap{{QStringLiteral("state"), QString::fromLatin1(samn::MotionStateName(s.state))},
                                {QStringLiteral("startMs"), s.start_ms},
                                {QStringLiteral("endMs"), s.end_ms},
                                {QStringLiteral("meanSpeed"), s.mean_speed}};
    }
    QVariantMap shares;
    for (const auto& item : analysis.share_by_state) shares.insert(FromU8(item.first), item.second);
    return {{QStringLiteral("segments"), segments},
            {QStringLiteral("shareByState"), shares},
            {QStringLiteral("totalMs"), analysis.total_ms},
            {QStringLiteral("summary"), FromU8(analysis.summary)},
            {QStringLiteral("error"), QString()}};
}

QVariantList PlayerController::scriptMotionChapters() {
    QVariantList out;
    for (const auto& c : ChaptersFor(stroke_, marks_.chapters)) {
        out << QVariantMap{{QStringLiteral("kind"), FromU8(c.kind)},
                           {QStringLiteral("startMs"), c.start_ms},
                           {QStringLiteral("endMs"), c.end_ms}};
    }
    return out;
}

std::vector<samn::Chapter> PlayerController::autoChaptersForSave() const {
    std::vector<samn::Chapter> out;
    const auto auto_ch = AutoFromStroke(stroke_);
    out.reserve(auto_ch.size());
    for (const auto& c : auto_ch) {
        samn::Chapter ch;
        ch.name = samn::MotionChapterLabel(c.kind);
        ch.start_ms = static_cast<int64_t>(std::llround(c.start_ms));
        ch.end_ms = static_cast<int64_t>(std::llround(c.end_ms));
        if (ch.end_ms <= ch.start_ms) ch.end_ms = ch.start_ms + 1;
        out.push_back(std::move(ch));
    }
    return out;
}

}  // namespace samn::ui
