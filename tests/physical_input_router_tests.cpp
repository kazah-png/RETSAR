#include "../src/lab/input-routing/physical_input_router.hpp"
#include <iostream>
#include <limits>
#include <type_traits>
using namespace retsar::lab::input;
namespace {
unsigned passed = 0, failed = 0;
void check(bool value, const char* message) {
    if (value) ++passed; else { ++failed; std::cerr << "FAIL " << message << '\n'; }
}
Identity testIdentity{11, 22, 33, 7};
Lease lease(std::uint64_t epoch = 1, std::uint64_t deadline = 1000) {
    return {testIdentity, epoch, deadline};
}
Event key(unsigned code, bool down) { return {Kind::Key, static_cast<std::uint16_t>(code), down, 0, 0}; }
Event button(unsigned code, bool down) { return {Kind::Button, static_cast<std::uint16_t>(code), down, 0, 0}; }
Event motion(int x = 1, int y = 2) { return {Kind::Motion, 0, false, x, y}; }
bool host(Decision d) { return d.destination == Destination::Host; }
bool target(Decision d) { return d.destination == Destination::TargetQueue; }
bool drain(Decision d) { return d.destination == Destination::SuppressUntilRelease; }
void Admission() {
    Router<> r(11);
    check(!r.Begin(lease(), 0, false), "neutral observation is mandatory");
    auto bad = lease(); bad.identity.device = 12;
    check(!r.Begin(bad, 0, true), "exact device incarnation required");
    bad = lease(); bad.identity.child = 0;
    check(!r.Begin(bad, 0, true), "child incarnation cannot be omitted");
    bad = lease(); bad.identity.broker = 0;
    check(!r.Begin(bad, 0, true), "broker incarnation cannot be omitted");
    bad = lease(); bad.identity.session = UINT32_MAX;
    check(!r.Begin(bad, 0, true), "sentinel session rejected");
    check(!r.Begin(lease(0), 0, true), "zero epoch rejected");
    check(!r.Begin(lease(1, 2001), 0, true), "lease lifetime bounded");
    check(!r.Begin(lease(1, 0), 0, true), "already expired lease rejected");
    check(host(r.Process(key(4, true), 1)), "unarmed input passes through");
    check(!r.Begin(lease(), 2, true), "host held key prevents acquisition");
    check(host(r.Process(key(4, false), 3)), "host release remains on host");
    check(r.Begin(lease(), 4, true), "neutral verified device can start");
    check(!r.Begin(lease(2), 5, true), "active lease cannot be replaced");
    r.Disconnect(testIdentity, 1);
    check(!r.ResetRequired(), "unused lease needs no child reset");
    check(!r.Begin(lease(1), 6, true), "epoch cannot be replayed");
    check(r.Begin(lease(2), 7, true), "new unused epoch can start");
    Router<> zero(0); check(!zero.Begin(lease(), 0, true), "unbound router cannot arm");
    Router<> unknown(11); check(unknown.Begin(lease(), 0, true), "unknown release setup");
    check(host(unknown.Process(key(4, false), 1)) && !unknown.Active(),
        "orphaned release revokes claimed neutral lease");
}
void QueueAndIdentity() {
    Router<3> r(11); Packet p;
    check(r.Begin(lease(), 0, true), "queue setup");
    check(target(r.Process(key(9, true), 1)), "make queues for target");
    check(target(r.Process(key(9, true), 2)), "repeat queues for same target");
    check(target(r.Process(key(9, false), 3)), "break queues for target");
    check(r.Pending() == 3 && !r.Held(), "bounded queue preserves transitions");
    auto wrong = testIdentity; wrong.broker++;
    check(!r.Pop(wrong, 1, 4, p) && r.Pending() == 3 && p.sequence == 0,
        "foreign broker cannot consume and gets cleared output");
    wrong = testIdentity; wrong.child++;
    check(!r.Pop(wrong, 1, 4, p), "reused child session testIdentity rejected");
    wrong = testIdentity; wrong.session++;
    check(!r.Pop(wrong, 1, 4, p), "different target session rejected");
    check(!r.Pop(testIdentity, 2, 4, p), "wrong epoch cannot consume");
    check(r.Pop(testIdentity, 1, 5, p) && p.sequence == 1 && p.epoch == 1 && p.event.down,
        "first packet is sequenced make");
    check(r.Pop(testIdentity, 1, 6, p) && p.sequence == 2 && p.event.down, "repeat order retained");
    check(target(r.Process(motion(-7, 8), 7)), "queue wraps without allocation");
    check(r.Pop(testIdentity, 1, 8, p) && p.sequence == 3 && !p.event.down, "release order retained");
    check(r.Pop(testIdentity, 1, 9, p) && p.sequence == 4 && p.event.x == -7 && p.event.y == 8,
        "motion payload retained only in bounded memory");
    check(!r.Pop(testIdentity, 1, 10, p) && p.sequence == 0, "empty pop clears stale payload");
}
void ExpiryAndPairs() {
    Router<8> r(11); Packet p;
    check(r.Begin(lease(1, 10), 0, true), "expiry setup");
    check(target(r.Process(key(12, true), 1)), "capture key before expiry");
    check(r.Pop(testIdentity, 1, 2, p), "child may already hold key");
    check(target(r.Process(button(0, true), 3)), "capture button before expiry");
    r.Tick(10);
    check(!r.Active() && r.ResetRequired() && r.ResetEpoch() == 1 && r.Pending() == 0,
        "timer expires idle route, flushes queue and demands child reset");
    check(r.LastFault() == Fault::Expired, "expiry reason exposed without key contents");
    check(drain(r.Process(key(12, true), 11)), "held repeat cannot leak to host");
    check(host(r.Process(key(13, true), 12)), "new key fails open immediately");
    check(host(r.Process(motion(), 13)), "motion fails open immediately");
    check(!r.Begin(lease(2, 100), 14, true), "unacknowledged reset prevents rearm");
    auto wrong = testIdentity; wrong.broker++;
    check(!r.AcknowledgeReset(wrong, 1) && !r.AcknowledgeReset(testIdentity, 2),
        "reset acknowledgement must match old owner and epoch");
    check(r.AcknowledgeReset(testIdentity, 1), "exact child reset acknowledgement accepted");
    check(!r.Begin(lease(2, 100), 15, true), "held input still prevents rearm after reset");
    check(drain(r.Process(key(12, false), 16)), "captured release cannot leak to host");
    check(drain(r.Process(button(0, false), 17)), "captured button release cannot leak to host");
    check(host(r.Process(key(13, false), 18)), "new host key keeps its own release");
    check(r.Begin(lease(2, 100), 19, true), "rearm needs release, reset, neutral proof and new epoch");
    check(!r.AcknowledgeReset(testIdentity, 1), "old reset cannot affect new live epoch");
}
void Overflow() {
    Router<1> r(11); Packet p;
    check(r.Begin(lease(), 0, true), "overflow setup");
    check(target(r.Process(key(1, true), 1)), "first make fits");
    check(drain(r.Process(key(1, false), 2)), "overflow on release suppresses orphan host break");
    check(!r.Active() && r.Pending() == 0 && r.ResetRequired() && !r.Held(),
        "overflow flushes old generation and requires reset even if make was unread");
    check(!r.Pop(testIdentity, 1, 3, p), "expired queue can never leak stale packet");
    check(r.AcknowledgeReset(testIdentity, 1) && r.Begin(lease(2), 4, true), "overflow can recover explicitly");
    check(target(r.Process(key(1, true), 5)), "new lease make captured");
    check(host(r.Process(key(2, true), 6)), "overflow on fresh key passes complete new pair to host");
    check(drain(r.Process(key(1, true), 7)), "previous captured repeat remains drained");
    check(drain(r.Process(key(1, false), 8)), "previous captured release remains drained");
    check(host(r.Process(key(2, false), 9)), "fresh key release stays with host make");
}
void Lifecycle() {
    Router<> r(11); check(r.Begin(lease(1, 10), 0, true), "renew setup");
    auto wrong = testIdentity; wrong.child++;
    check(!r.Renew(wrong, 1, 30, 1), "foreign renew rejected");
    check(!r.Renew(testIdentity, 2, 30, 2), "old epoch cannot renew");
    check(!r.Renew(testIdentity, 1, 9, 3), "renew cannot shorten lease");
    check(!r.Renew(testIdentity, 1, 3000, 4), "renew cannot make unbounded lease");
    check(r.Renew(testIdentity, 1, 20, 5), "current owner renews bounded live lease");
    r.Disconnect(wrong, 1); check(r.Active(), "unrelated disconnect ignored");
    check(target(r.Process(key(7, true), 6)), "lifecycle make");
    r.Disconnect(testIdentity, 1);
    check(!r.Active() && r.ResetRequired() && r.LastFault() == Fault::Disconnected,
        "broker or child loss revokes ownership and requests reset");
    r.Remove(); check(!r.Held(), "device removal forgets impossible old releases");
    check(r.AcknowledgeReset(testIdentity, 1), "removed incarnation can clear its reset debt");
    check(!r.Begin(lease(2, 30), 7, true), "removed incarnation can never rearm");
    check(r.Process(key(8, true), 8).destination == Destination::DiscardStaleDevice, "removed incarnation cannot leak delayed make to host");
    check(r.Process(key(7, false), 9).destination == Destination::DiscardStaleDevice, "removed incarnation cannot leak delayed break to host");
    Router<> late(11); check(late.Begin(lease(1, 10), 0, true), "late renew setup");
    check(!late.Renew(testIdentity, 1, 30, 10) && !late.Active(), "renew at deadline cannot revive expired lease");
    Router<> clock(11); check(clock.Begin(lease(), 10, true), "clock setup");
    clock.Tick(9);
    check(!clock.Active() && clock.LastFault() == Fault::ClockRegressed, "clock reversal fails open");
    check(!clock.Begin(lease(2), 11, true), "clock uncertainty prevents rearm on old router");
    Router<> unsupported(11); check(unsupported.Begin(lease(), 0, true), "unsupported setup");
    check(host(unsupported.Process({static_cast<Kind>(255), 0, false, 0, 0}, 1)) && !unsupported.Active(),
        "unknown event passes through and revokes route");
    Router<> invalidKey(11); check(invalidKey.Begin(lease(), 0, true), "invalid key setup");
    check(host(invalidKey.Process(key(768, true), 1)) && !invalidKey.Active(), "invalid key cannot index state");
    Router<> nearMax(11); const auto maximum = std::numeric_limits<std::uint64_t>::max();
    check(nearMax.Begin(lease(1, maximum), maximum - 10, true), "deadline checks avoid addition overflow");
    nearMax.Tick(maximum); check(!nearMax.Active(), "maximum deadline expires exactly");
}
void MixedSequenceInvariant() {
    // Deterministic transition/repeat/failure stream, verifying that every host
    // release retains its own host make, including queue saturation and rearm.
    Router<4> r(11); bool physical[8]{}, hostHeld[8]{}; unsigned random = 0x51a3;
    std::uint64_t epoch = 1; bool pairsConsistent = true, queueBounded = true;
    r.Begin(lease(epoch, 100), 0, true);
    for (std::uint64_t time = 1; time != 10001; ++time) {
        random = random * 1664525u + 1013904223u;
        const unsigned code = (random >> 16) % 8;
        const bool down = !physical[code] || ((random >> 24) % 4 == 0);
        physical[code] = down;
        const auto decision = r.Process(code < 4 ? key(code, down) : button(code - 4, down), time);
        if (host(decision)) {
            if (!down && !hostHeld[code]) pairsConsistent = false;
            hostHeld[code] = down;
        }
        queueBounded = queueBounded && r.Pending() <= 4;
        if (time % 13 == 0) r.Disconnect(testIdentity, epoch);
        if (r.ResetRequired()) r.AcknowledgeReset(testIdentity, epoch);
        if (!r.Active() && !r.Held()) r.Begin(lease(++epoch, time + 100), time, true);
    }
    check(pairsConsistent, "10000 mixed transitions never send an orphaned host release");
    check(queueBounded, "10000 mixed transitions never exceed queue capacity");
}
}
int main() {
    static_assert(std::is_trivially_copyable<Packet>::value, "queue packets require fixed value layout");
    Admission(); QueueAndIdentity(); ExpiryAndPairs(); Overflow(); Lifecycle(); MixedSequenceInvariant();
    std::cout << "Physical input router: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}