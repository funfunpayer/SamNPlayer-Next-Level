#pragma once
// The Editor's model: the curves of one script (one per axis) plus an undo
// history (D-082). An undo step stores only the changed window of each axis
// (a patch), not the whole axis, so a 100 000-point script stays cheap to
// edit (D-083). The history has a hard size limit; the oldest steps go
// first (D-142).
//
// Point rules carried over from 0.7 (playback.js):
//   - a dragged point stays between its neighbours (1 ms apart) and inside
//     [0, duration];
//   - positions are clamped to 0..100;
//   - deleting a point keeps at least 2 points in the axis;
//   - a drag that ends where it started records no step.
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "samn/funscript.h"

namespace samn {

// One axis window: actions [first, first + removed.size()) were replaced by
// `added`.
struct AxisPatch {
    std::string axis;
    size_t first = 0;
    std::vector<Action> removed;
    std::vector<Action> added;
};

struct EditStep {
    std::string label;
    std::vector<AxisPatch> patches;
    uint64_t id = 0;
    [[nodiscard]] size_t Bytes() const;
};

struct HistoryLimits {
    size_t max_steps = 200;
    size_t max_bytes = size_t{64} << 20;
};

class EditSession {
public:
    using Axes = std::map<std::string, std::vector<Action>>;

    explicit EditSession(Axes axes = {}, HistoryLimits limits = {});

    [[nodiscard]] const Axes& axes() const { return axes_; }
    // The actions of `axis`; empty when the axis does not exist.
    [[nodiscard]] const std::vector<Action>& Axis(const std::string& axis) const;

    // Everything between Begin and Commit is one undo step (a drag, a
    // multi-axis edit). Commit returns false and records nothing when the
    // curves did not change. Rollback restores the state of Begin.
    void Begin(std::string label);
    bool Commit();
    void Rollback();
    [[nodiscard]] bool InTransaction() const { return txn_.has_value(); }

    // Replaces a whole axis (range tools, invert, generated curves). Outside
    // a transaction this is one undo step labelled `label`.
    void Replace(const std::string& axis, std::vector<Action> actions, const std::string& label);

    // Moves point `index` to (at, pos), clamped as described above;
    // duration_ms <= 0 means no upper bound. Returns false for a bad index.
    bool MovePoint(const std::string& axis, size_t index, int64_t at, int pos, int64_t duration_ms);

    // Inserts a point in time order and returns its index. A point that
    // already sits at `at` takes the new position instead.
    size_t AddPoint(const std::string& axis, int64_t at, int pos);

    // Removes point `index`. Returns false (and changes nothing) for a bad
    // index or when fewer than 2 points would remain.
    bool DeletePoint(const std::string& axis, size_t index);

    bool Undo();
    bool Redo();
    [[nodiscard]] bool CanUndo() const { return !txn_ && !undo_.empty(); }
    [[nodiscard]] bool CanRedo() const { return !txn_ && !redo_.empty(); }
    [[nodiscard]] std::string UndoLabel() const { return undo_.empty() ? std::string() : undo_.back().label; }
    [[nodiscard]] std::string RedoLabel() const { return redo_.empty() ? std::string() : redo_.back().label; }
    [[nodiscard]] size_t UndoCount() const { return undo_.size(); }
    [[nodiscard]] size_t HistoryBytes() const { return history_bytes_; }

    // The state that was last saved; dirty() is true when the curves differ
    // from it by at least one step.
    void MarkSaved();
    [[nodiscard]] bool dirty() const;

private:
    struct Transaction {
        std::string label;
        Axes originals;  // each axis as it was when the transaction first touched it
    };

    std::vector<Action>& Touch(const std::string& axis);
    void Push(EditStep step);
    void Trim();
    void Apply(const EditStep& step, bool forward);
    // Names the current state: the id of the newest undo step, or of the
    // last step trimmed away when the undo stack is empty.
    [[nodiscard]] uint64_t TopId() const { return undo_.empty() ? base_id_ : undo_.back().id; }

    Axes axes_;
    HistoryLimits limits_;
    std::vector<EditStep> undo_;
    std::vector<EditStep> redo_;
    std::optional<Transaction> txn_;
    size_t history_bytes_ = 0;
    uint64_t next_id_ = 1;
    uint64_t base_id_ = 0;
    uint64_t saved_id_ = 0;
};

// The patch that turns `before` into `after` (common prefix and suffix cut).
[[nodiscard]] AxisPatch DiffAxis(const std::string& axis, const std::vector<Action>& before,
                                 const std::vector<Action>& after);

}  // namespace samn
