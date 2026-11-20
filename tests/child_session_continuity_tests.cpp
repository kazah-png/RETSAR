#include "../src/lab/child_session_continuity_contract.hpp"
#include <iostream>
using namespace seats::lab::child::continuity;
namespace {
unsigned passed = 0, failed = 0;
void Check(bool value, const char* name) {
    if (value) ++passed; else { ++failed; std::cerr << "FAIL " << name << '\n'; }
}
std::vector<std::wstring> Arguments() {
    return {L"--duration-seconds", L"30", L"--expected-session", L"7",
        L"--reference-session", L"6", L"--receipt", L"synthetic.jsonl"};
}
bool Rejects(const std::vector<std::wstring>& args) {
    try { (void)Parse(args); return false; } catch (const std::invalid_argument&) { return true; }
}
Identity GoodIdentity() { return {true, true, true, 7, 7, 6, true, true}; }
struct Fake {
    Identity value = GoodIdentity(); unsigned calls = 0; DWORD firstActive = MAXDWORD, secondActive = MAXDWORD;
    static bool Process(void* context, DWORD& result) noexcept {
        auto& fake = *static_cast<Fake*>(context); ++fake.calls;
        result = fake.value.process; return fake.value.processKnown;
    }
    static bool Token(void* context, DWORD& result) noexcept {
        auto& fake = *static_cast<Fake*>(context); ++fake.calls;
        result = fake.value.token; return fake.value.tokenKnown;
    }
    static bool NotImpersonating(void* context) noexcept {
        auto& fake = *static_cast<Fake*>(context); ++fake.calls; return fake.value.noImpersonation;
    }
    static DWORD Console(void* context) noexcept {
        auto& fake = *static_cast<Fake*>(context); ++fake.calls; return fake.value.console;
    }
    static bool Active(void* context, DWORD session) noexcept {
        auto& fake = *static_cast<Fake*>(context); ++fake.calls;
        if (fake.firstActive == MAXDWORD) fake.firstActive = session; else fake.secondActive = session;
        return session == 7 ? fake.value.expectedActive : session == 6 && fake.value.referenceActive;
    }
    IdentityApi Api() noexcept { return {this, Process, Token, NotImpersonating, Console, Active}; }
};
void ArgumentTests() {
    Check(!Parse({}).run, "default has no operation");
    Check(!Parse({L"--help"}).run, "help has no operation");
    auto args = Arguments(); auto options = Parse(args);
    Check(options.run && options.seconds == 30 && options.expected == 7 && options.reference == 6,
        "explicit bounded invocation parses");
    args[1] = L"120"; Check(Parse(args).seconds == 120, "upper duration bound accepted");
    for (const auto* duration : {L"0", L"29", L"121", L"-1", L"+30", L"30x", L" 30", L"", L"4294967296", L"99999999999999999999"}) {
        args = Arguments(); args[1] = duration;
        Check(Rejects(args), "invalid duration rejected");
    }
    for (const auto* session : {L"0", L"4294967295", L"4294967296", L"-7", L"+7", L"7x", L""}) {
        args = Arguments(); args[3] = session; Check(Rejects(args), "invalid expected session rejected");
        args = Arguments(); args[5] = session; Check(Rejects(args), "invalid reference session rejected");
    }
    args = Arguments(); args[3] = L"6"; Check(Rejects(args), "same expected and reference sessions rejected");
    args = Arguments(); args[7] = L""; Check(Rejects(args), "empty receipt rejected");
    args = Arguments(); args[7] = std::wstring(2049, L'x'); Check(Rejects(args), "oversized receipt rejected");
    args = Arguments(); args.push_back(L"--help"); Check(Rejects(args), "help cannot conceal an operation");
    args = Arguments(); args.pop_back(); Check(Rejects(args), "missing receipt rejected");
    args = Arguments(); args[4] = L"--expected-session"; Check(Rejects(args), "duplicate option rejected");
    args = Arguments(); args[0] = L"--run"; Check(Rejects(args), "unrecognized operation rejected");
}
void GateTests() {
    const auto options = Parse(Arguments()); auto state = GoodIdentity();
    Check(Allowed(options, state), "exact active child and parent metadata admitted");
    Check(!Allowed({}, state), "inert options cannot admit metadata");
    for (unsigned missing = 0; missing != 5; ++missing) {
        state = GoodIdentity();
        switch (missing) {
        case 0: state.processKnown = false; break;
        case 1: state.tokenKnown = false; break;
        case 2: state.noImpersonation = false; break;
        case 3: state.expectedActive = false; break;
        case 4: state.referenceActive = false; break;
        }
        Check(!Allowed(options, state), "unknown identity or inactive endpoint rejected");
    }
    state = GoodIdentity(); state.process = 6; Check(!Allowed(options, state), "parent process cannot run child probe");
    state = GoodIdentity(); state.token = 6; Check(!Allowed(options, state), "parent token cannot run child probe");
    state = GoodIdentity(); state.console = 7; Check(!Allowed(options, state), "child cannot be active console reference");
    state = GoodIdentity(); state.console = MAXDWORD; Check(!Allowed(options, state), "missing console is not accepted");
    state = GoodIdentity(); state.process = 8; Check(!Allowed(options, state), "unrelated active session rejected");
    auto invalidOptions = options; invalidOptions.seconds = 121;
    Check(!Allowed(invalidOptions, GoodIdentity()), "hand-built oversized duration rejected by gate too");
    Fake fake;
    Check(!Allowed({}, ReadIdentity({}, fake.Api())) && fake.calls == 0,
        "default does not call even injected metadata APIs");
    Check(!Allowed(invalidOptions, ReadIdentity(invalidOptions, fake.Api())) && fake.calls == 0,
        "invalid options do not call injected metadata APIs");
    for (unsigned absent = 0; absent != 5; ++absent) {
        auto api = fake.Api();
        switch (absent) {
        case 0: api.process = nullptr; break;
        case 1: api.token = nullptr; break;
        case 2: api.noImpersonation = nullptr; break;
        case 3: api.console = nullptr; break;
        case 4: api.active = nullptr; break;
        }
        Check(!Allowed(options, ReadIdentity(options, api)) && fake.calls == 0,
            "incomplete API rejects before any observation");
    }
    Check(Allowed(options, ReadIdentity(options, fake.Api())) && fake.calls == 6 &&
        fake.firstActive == 7 && fake.secondActive == 6, "only requested two active sessions are queried");
    fake.value.tokenKnown = false;
    Check(!Allowed(options, ReadIdentity(options, fake.Api())), "injected token query failure remains failure");
}
void CounterTests() {
    const auto options = Parse(Arguments());
    Check(!DurationReached(options, 29999), "deadline is not rounded up early");
    Check(DurationReached(options, 30000), "deadline reached exactly");
    Check(DurationReached(options, 31000), "delayed timer still expires");
    Check(!DurationReached({}, 999999), "inert options cannot complete");
    Counters counters;
    counters.Tick(250); counters.Paint(260); counters.Tick(500); counters.Paint(520);
    Check(counters.ticks == 2 && counters.paints == 2 && counters.lastTickMs == 500 &&
        counters.lastPaintMs == 520, "timer and local paint counters are independent");
    Check(counters.maxTickGapMs == 250 && counters.maxPaintGapMs == 260,
        "initial scheduling and subsequent gaps retained");
    counters.Finish(1300);
    Check(counters.maxTickGapMs == 800 && counters.maxPaintGapMs == 780,
        "final silence included even without another paint or tick");
    counters.Tick(499); Check(!counters.monotonic && counters.ticks == 2, "reversed tick never increments count");
    Counters reversedPaint; reversedPaint.Paint(100); reversedPaint.Paint(99);
    Check(!reversedPaint.monotonic && reversedPaint.paints == 1, "reversed paint rejected");
    Counters reversedFinish; reversedFinish.Tick(100); reversedFinish.Finish(99);
    Check(!reversedFinish.monotonic, "finish cannot hide clock reversal");
    Counters quiet; quiet.Finish(30000);
    Check(quiet.ticks == 0 && quiet.paints == 0 && quiet.maxPaintGapMs == 30000,
        "no observed work stays distinct from elapsed duration");
}
}
int main() {
    ArgumentTests(); GateTests(); CounterTests();
    std::cout << "Child session continuity: " << passed << " passed, " << failed
        << " failed; supplied values only, no windows, input or session queries.\n";
    return failed ? 1 : 0;
}