#pragma once
#include "desktop_model.hpp"
#include <cstdint>
#include <cwchar>
#include <string>

namespace retsar::desktop::intent {
constexpr std::uint64_t DeadlineMs = 10000;
struct ViewerKey {
    std::uintptr_t window = 0;
    DWORD process = 0;
    std::uint64_t created = 0;
    std::wstring path;
};
struct Target {
    bool valid = false;
    Decision decision{};
    DWORD caller = MAXDWORD, console = MAXDWORD;
    ViewerKey viewer{};
};
inline bool KnownCaller(const Facts& facts) noexcept {
    const auto& session = facts.session;
    if (facts.designPreview || facts.refreshing || facts.launchPending || !facts.inventoryComplete ||
        facts.rows.empty() || facts.rows.size() > 1024 || !session.caller.attempted ||
        !session.caller.succeeded || !session.caller.value || session.caller.value == MAXDWORD ||
        !session.console.attempted || session.console.value != session.caller.value) return false;
    unsigned callerRows = 0;
    for (std::size_t i = 0; i < facts.rows.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) if (facts.rows[i].id == facts.rows[j].id) return false;
        if (facts.rows[i].id == session.caller.value) {
            if (facts.rows[i].state != WTSActive || !facts.rows[i].hasUser) return false;
            ++callerRows;
        }
    }
    return callerRows == 1;
}
// Only facts relevant to an action enter this boundary. Display, memory and
// physical-input inventory failures do not authorize or veto desktop actions.
inline Target Capture(const Facts& facts, const ViewerKey& viewer = {}, bool outstandingLaunch = false) {
    Target result;
    if (!KnownCaller(facts)) return result;
    result.decision = Decide(facts);
    if (!CanLaunch(result.decision) && result.decision.action != Action::ShowViewer) return result;
    if (outstandingLaunch && CanLaunch(result.decision)) return result;
    if (result.decision.action == Action::ShowViewer) {
        if (!facts.verifiedViewerPresent || !viewer.window || !viewer.process ||
            !viewer.created || viewer.path.empty()) return result;
        result.viewer = viewer;
    }
    result.caller = facts.session.caller.value; result.console = facts.session.console.value;
    result.valid = true; return result;
}
inline bool ValidTarget(const Target& target) noexcept {
    if (!target.valid || !target.caller || target.caller == MAXDWORD || target.console != target.caller) return false;
    if (target.decision.action == Action::CreateDesktop) return target.decision.child == MAXDWORD;
    if (IsResume(target.decision)) return target.decision.child && target.decision.child != MAXDWORD && target.decision.child != target.caller;
    if (target.decision.action != Action::ShowViewer) return false;
    return target.viewer.window && target.viewer.process && target.viewer.created && !target.viewer.path.empty();
}
inline bool Same(const Target& before, const Target& after) noexcept {
    if (!ValidTarget(before) || !ValidTarget(after) || before.decision.action != after.decision.action ||
        before.decision.child != after.decision.child || before.caller != after.caller ||
        before.console != after.console) return false;
    if (before.decision.action != Action::ShowViewer) return true;
    return before.viewer.window == after.viewer.window && before.viewer.process == after.viewer.process &&
        before.viewer.created == after.viewer.created &&
        _wcsicmp(before.viewer.path.c_str(), after.viewer.path.c_str()) == 0;
}
enum class Outcome { None, Waiting, Ready, Cancelled };
enum class Reason { None, Explicit, Inactive, Closing, Busy, Timeout, ClockChanged, Changed, Uncertain };
class Ticket {
    bool pending_ = false;
    Target target_{};
    std::uint64_t clicked_ = 0, minimumGeneration_ = 0;
    Reason reason_ = Reason::None;
public:
    bool Pending() const noexcept { return pending_; }
    Reason Why() const noexcept { return reason_; }
    const Target& Requested() const noexcept { return target_; }
    bool Begin(const Target& target, std::uint64_t now, std::uint64_t minimumGeneration) {
        if (pending_ || !ValidTarget(target) || !minimumGeneration) return false;
        target_ = target; clicked_ = now; minimumGeneration_ = minimumGeneration;
        reason_ = Reason::None; pending_ = true; return true;
    }
    void Cancel(Reason reason = Reason::Explicit) noexcept { pending_ = false; reason_ = reason; }
    Outcome Poll(std::uint64_t now, bool active, bool closing, bool launchPending) noexcept {
        if (!pending_) return Outcome::None;
        if (closing) Cancel(Reason::Closing);
        else if (!active) Cancel(Reason::Inactive);
        else if (launchPending) Cancel(Reason::Busy);
        else if (now < clicked_) Cancel(Reason::ClockChanged);
        else if (now - clicked_ >= DeadlineMs) Cancel(Reason::Timeout);
        return pending_ ? Outcome::Waiting : Outcome::Cancelled;
    }
    Outcome Resolve(const Target& fresh, std::uint64_t generation, std::uint64_t readStarted,
        std::uint64_t now, bool active, bool closing, bool launchPending) noexcept {
        const auto eligible = Poll(now, active, closing, launchPending);
        if (eligible != Outcome::Waiting) return eligible;
        if (!ValidTarget(fresh)) { Cancel(Reason::Uncertain); return Outcome::Cancelled; }
        if (!Same(target_, fresh)) { Cancel(Reason::Changed); return Outcome::Cancelled; }
        if (readStarted > now) { Cancel(Reason::ClockChanged); return Outcome::Cancelled; }
        if (generation < minimumGeneration_ || readStarted < clicked_) return Outcome::Waiting;
        // Consume BEFORE the caller can enter an elevation prompt, COM callback,
        // window activation, or another message loop. There is no implicit retry.
        pending_ = false; reason_ = Reason::None; return Outcome::Ready;
    }
};
} // namespace retsar::desktop::intent