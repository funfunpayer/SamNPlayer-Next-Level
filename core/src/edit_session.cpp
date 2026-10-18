#include "samn/edit_session.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace samn {
namespace {

bool Same(const Action& a, const Action& b) { return a.at == b.at && a.pos == b.pos; }

int ClampPos(int pos) { return std::clamp(pos, 0, 100); }

const std::vector<Action>& Empty() {
    static const std::vector<Action> kEmpty;
    return kEmpty;
}

}  // namespace

size_t EditStep::Bytes() const {
    size_t bytes = sizeof(EditStep) + label.size();
    for (const AxisPatch& p : patches)
        bytes += sizeof(AxisPatch) + p.axis.size() + (p.removed.size() + p.added.size()) * sizeof(Action);
    return bytes;
}

AxisPatch DiffAxis(const std::string& axis, const std::vector<Action>& before,
                   const std::vector<Action>& after) {
    size_t prefix = 0;
    const size_t shorter = std::min(before.size(), after.size());
    while (prefix < shorter && Same(before[prefix], after[prefix])) ++prefix;
    size_t suffix = 0;
    while (suffix < shorter - prefix &&
           Same(before[before.size() - 1 - suffix], after[after.size() - 1 - suffix]))
        ++suffix;
    AxisPatch patch;
    patch.axis = axis;
    patch.first = prefix;
    patch.removed.assign(before.begin() + static_cast<std::ptrdiff_t>(prefix),
                         before.end() - static_cast<std::ptrdiff_t>(suffix));
    patch.added.assign(after.begin() + static_cast<std::ptrdiff_t>(prefix),
                       after.end() - static_cast<std::ptrdiff_t>(suffix));
    return patch;
}

EditSession::EditSession(Axes axes, HistoryLimits limits) : axes_(std::move(axes)), limits_(limits) {}

const std::vector<Action>& EditSession::Axis(const std::string& axis) const {
    const auto it = axes_.find(axis);
    return it == axes_.end() ? Empty() : it->second;
}

void EditSession::Begin(std::string label) {
    if (txn_) Commit();
    txn_ = Transaction{std::move(label), {}};
}

bool EditSession::Commit() {
    if (!txn_) return false;
    EditStep step;
    step.label = std::move(txn_->label);
    for (const auto& [axis, original] : txn_->originals) {
        AxisPatch patch = DiffAxis(axis, original, axes_[axis]);
        if (!patch.removed.empty() || !patch.added.empty()) step.patches.push_back(std::move(patch));
    }
    txn_.reset();
    if (step.patches.empty()) return false;
    Push(std::move(step));
    return true;
}

void EditSession::Rollback() {
    if (!txn_) return;
    for (auto& [axis, original] : txn_->originals) axes_[axis] = std::move(original);
    txn_.reset();
}

std::vector<Action>& EditSession::Touch(const std::string& axis) {
    std::vector<Action>& actions = axes_[axis];
    if (txn_ && txn_->originals.find(axis) == txn_->originals.end()) txn_->originals.emplace(axis, actions);
    return actions;
}

void EditSession::Replace(const std::string& axis, std::vector<Action> actions, const std::string& label) {
    const bool own = !txn_;
    if (own) Begin(label);
    Touch(axis) = std::move(actions);
    if (own) Commit();
}

bool EditSession::MovePoint(const std::string& axis, size_t index, int64_t at, int pos, int64_t duration_ms) {
    if (index >= Axis(axis).size()) return false;
    const bool own = !txn_;
    if (own) Begin("move point");
    std::vector<Action>& actions = Touch(axis);
    const int64_t lower = index > 0 ? actions[index - 1].at + 1 : 0;
    const int64_t upper = index + 1 < actions.size() ? actions[index + 1].at - 1
                          : duration_ms > 0           ? duration_ms
                                                      : std::numeric_limits<int64_t>::max();
    at = std::max<int64_t>(at, 0);
    if (duration_ms > 0) at = std::min(at, duration_ms);
    // Neighbours 1 ms apart leave no room (0.7: then the time is not clamped).
    if (lower <= upper) at = std::clamp(at, lower, upper);
    actions[index] = Action{at, ClampPos(pos)};
    if (own) Commit();
    return true;
}

size_t EditSession::AddPoint(const std::string& axis, int64_t at, int pos) {
    const bool own = !txn_;
    if (own) Begin("add point");
    std::vector<Action>& actions = Touch(axis);
    at = std::max<int64_t>(at, 0);
    const auto it = std::lower_bound(actions.begin(), actions.end(), at,
                                     [](const Action& a, int64_t t) { return a.at < t; });
    const auto index = static_cast<size_t>(it - actions.begin());
    if (it != actions.end() && it->at == at)
        it->pos = ClampPos(pos);
    else
        actions.insert(it, Action{at, ClampPos(pos)});
    if (own) Commit();
    return index;
}

bool EditSession::DeletePoint(const std::string& axis, size_t index) {
    const std::vector<Action>& current = Axis(axis);
    if (index >= current.size() || current.size() <= 2) return false;
    const bool own = !txn_;
    if (own) Begin("delete point");
    std::vector<Action>& actions = Touch(axis);
    actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(index));
    if (own) Commit();
    return true;
}

void EditSession::Apply(const EditStep& step, bool forward) {
    auto apply = [this, forward](const AxisPatch& p) {
        std::vector<Action>& actions = axes_[p.axis];
        const std::vector<Action>& out = forward ? p.removed : p.added;
        const std::vector<Action>& in = forward ? p.added : p.removed;
        const auto first = actions.begin() + static_cast<std::ptrdiff_t>(p.first);
        actions.erase(first, first + static_cast<std::ptrdiff_t>(out.size()));
        actions.insert(actions.begin() + static_cast<std::ptrdiff_t>(p.first), in.begin(), in.end());
    };
    if (forward)
        std::for_each(step.patches.begin(), step.patches.end(), apply);
    else
        std::for_each(step.patches.rbegin(), step.patches.rend(), apply);
}

bool EditSession::Undo() {
    if (!CanUndo()) return false;
    EditStep step = std::move(undo_.back());
    undo_.pop_back();
    Apply(step, false);
    redo_.push_back(std::move(step));
    return true;
}

bool EditSession::Redo() {
    if (!CanRedo()) return false;
    EditStep step = std::move(redo_.back());
    redo_.pop_back();
    Apply(step, true);
    undo_.push_back(std::move(step));
    return true;
}

void EditSession::Push(EditStep step) {
    step.id = next_id_++;
    for (const EditStep& r : redo_) history_bytes_ -= r.Bytes();
    redo_.clear();
    history_bytes_ += step.Bytes();
    undo_.push_back(std::move(step));
    Trim();
}

void EditSession::Trim() {
    // The newest step always stays, however large it is.
    while (undo_.size() > 1 && (undo_.size() > limits_.max_steps || history_bytes_ > limits_.max_bytes)) {
        history_bytes_ -= undo_.front().Bytes();
        base_id_ = undo_.front().id;
        undo_.erase(undo_.begin());
    }
}

void EditSession::MarkSaved() { saved_id_ = TopId(); }

bool EditSession::dirty() const { return TopId() != saved_id_; }

}  // namespace samn
