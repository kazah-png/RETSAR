#pragma once
#include <cstddef>
#include <cstdint>

namespace retsar::lab::input {
// In-memory, allocation-free policy only. This does not intercept Windows input,
// authenticate a broker, assign a session, or establish physical exclusivity.
// The adapter must serialize every call (including its timer and consumer).
enum class Kind : std::uint8_t { Key, Button, Motion, Wheel };
struct Event {
    Kind kind = Kind::Motion;
    std::uint16_t code = 0; // Key: scan + E0/E1 bank; button: 0..4; wheel: 0..1.
    bool down = false;
    std::int32_t x = 0, y = 0;
};
struct Identity {
    std::uint64_t device = 0, child = 0, broker = 0;
    std::uint32_t session = 0;
};
inline bool Same(const Identity& a, const Identity& b) noexcept {
    return a.device == b.device && a.child == b.child &&
        a.broker == b.broker && a.session == b.session;
}
inline bool Valid(const Identity& value) noexcept {
    return value.device && value.child && value.broker && value.session &&
        value.session != UINT32_MAX;
}
struct Lease { Identity identity{}; std::uint64_t epoch = 0, deadlineMs = 0; };
struct Packet { std::uint64_t epoch = 0, sequence = 0; Event event{}; };
enum class Fault : std::uint8_t {
    None, Expired, QueueFull, Disconnected, Removed, ClockRegressed, UnsupportedEvent
};
enum class Destination : std::uint8_t { Host, TargetQueue, SuppressUntilRelease, DiscardStaleDevice };
struct Decision { Destination destination = Destination::Host; Fault fault = Fault::None; };

// Deadlines use one monotonic millisecond clock. Its caller must also run Tick
// independently of input arrivals, otherwise a silent device cannot expire.
template<std::size_t Capacity = 64>
class Router {
    static_assert(Capacity > 0 && Capacity <= 1024, "bounded nonempty queue required");
    enum class Owner : std::uint8_t { Neutral, Host, Target, Draining };
    static constexpr std::size_t KeyCount = 768, ButtonCount = 5;
public:
    static constexpr std::uint64_t MaxLeaseMs = 2000;
    explicit Router(std::uint64_t deviceIncarnation) noexcept : device_(deviceIncarnation) {}
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    // neutralVerified is an external, trustworthy per-device neutral check; a
    // quiet interval or absence of observed packets is NOT that proof. Admission
    // also needs an independently authenticated exact child and a spare pair.
    bool Begin(const Lease& lease, std::uint64_t now, bool neutralVerified) noexcept {
        Tick(now);
        if (removed_ || active_ || resetRequired_ || AnyHeld() || !neutralVerified ||
            !Valid(lease.identity) || lease.identity.device != device_ || !device_ ||
            !lease.epoch || lease.epoch <= lastEpoch_ || !DeadlineValid(lease.deadlineMs, now) ||
            clockRegressed_) return false;
        lease_ = lease; lastEpoch_ = lease.epoch; sequence_ = 0;
        active_ = true; fault_ = Fault::None; routed_ = false;
        return true;
    }
    bool Renew(const Identity& identity, std::uint64_t epoch,
        std::uint64_t deadline, std::uint64_t now) noexcept {
        Tick(now);
        if (!active_ || !Matches(identity, epoch) || !DeadlineValid(deadline, now) ||
            deadline < lease_.deadlineMs) return false;
        lease_.deadlineMs = deadline; return true;
    }
    void Tick(std::uint64_t now) noexcept {
        if (hasClock_ && now < lastNow_) {
            clockRegressed_ = true; Trip(Fault::ClockRegressed); return;
        }
        hasClock_ = true; lastNow_ = now;
        if (active_ && now >= lease_.deadlineMs) Trip(Fault::Expired);
    }
    void Disconnect(const Identity& identity, std::uint64_t epoch) noexcept {
        if (Matches(identity, epoch)) Trip(Fault::Disconnected);
    }
    void Remove() noexcept {
        Trip(Fault::Removed); removed_ = true;
        // The old device incarnation cannot produce another release. A new PnP
        // arrival requires a new router and independent neutral verification.
        for (auto& owner : owners_) owner = Owner::Neutral;
    }
    bool AcknowledgeReset(const Identity& identity, std::uint64_t epoch) noexcept {
        if (active_ || !resetRequired_ || !Matches(identity, epoch)) return false;
        resetRequired_ = false; return true;
    }
    Decision Process(const Event& event, std::uint64_t now) noexcept {
        Tick(now);
        if (removed_) return {Destination::DiscardStaleDevice, fault_};
        if (!ValidEvent(event)) {
            Trip(Fault::UnsupportedEvent); return {Destination::Host, fault_};
        }
        const bool transition = event.kind == Kind::Key || event.kind == Kind::Button;
        if (!transition) {
            if (active_ && Enqueue(event)) return {Destination::TargetQueue, fault_};
            return {Destination::Host, fault_};
        }
        auto& owner = owners_[Index(event)];
        if (owner == Owner::Host) {
            if (!event.down) owner = Owner::Neutral;
            return {Destination::Host, fault_};
        }
        if (owner == Owner::Draining) {
            if (!event.down) owner = Owner::Neutral;
            return {Destination::SuppressUntilRelease, fault_};
        }
        if (owner == Owner::Target) {
            if (active_ && Enqueue(event)) {
                if (!event.down) owner = Owner::Neutral;
                return {Destination::TargetQueue, fault_};
            }
            // Failure may have converted Target to Draining. Never forward an
            // orphaned repeat/release to the host after suppressing its make.
            if (!event.down) owner = Owner::Neutral;
            else owner = Owner::Draining;
            return {Destination::SuppressUntilRelease, fault_};
        }
        if (!event.down) {
            // A break without an observed make is incompatible with admission's
            // neutral claim. Preserve the host path and revoke the lease.
            if (active_) Trip(Fault::UnsupportedEvent);
            return {Destination::Host, fault_};
        }
        if (active_ && Enqueue(event)) {
            owner = Owner::Target; return {Destination::TargetQueue, fault_};
        }
        owner = Owner::Host;
        return {Destination::Host, fault_};
    }
    bool Pop(const Identity& identity, std::uint64_t epoch,
        std::uint64_t now, Packet& packet) noexcept {
        packet = {}; Tick(now);
        if (!active_ || !Matches(identity, epoch) || !count_) return false;
        packet = queue_[head_]; queue_[head_] = {};
        head_ = (head_ + 1) % Capacity; --count_; return true;
    }
    bool Active() const noexcept { return active_; }
    bool ResetRequired() const noexcept { return resetRequired_; }
    std::uint64_t ResetEpoch() const noexcept { return resetRequired_ ? lease_.epoch : 0; }
    Fault LastFault() const noexcept { return fault_; }
    std::size_t Pending() const noexcept { return count_; }
    bool Held() const noexcept { return AnyHeld(); }
private:
    static bool DeadlineValid(std::uint64_t deadline, std::uint64_t now) noexcept {
        return deadline > now && deadline - now <= MaxLeaseMs;
    }
    static bool ValidEvent(const Event& event) noexcept {
        switch (event.kind) {
        case Kind::Key: return event.code < KeyCount;
        case Kind::Button: return event.code < ButtonCount;
        case Kind::Motion: return event.code == 0 && !event.down;
        case Kind::Wheel: return event.code < 2 && !event.down;
        default: return false;
        }
    }
    static std::size_t Index(const Event& event) noexcept {
        return event.kind == Kind::Button ? KeyCount + event.code : event.code;
    }
    bool Matches(const Identity& identity, std::uint64_t epoch) const noexcept {
        return epoch && epoch == lease_.epoch && Same(identity, lease_.identity);
    }
    bool AnyHeld() const noexcept {
        for (const auto owner : owners_) if (owner != Owner::Neutral) return true;
        return false;
    }
    bool Enqueue(const Event& event) noexcept {
        if (count_ == Capacity || sequence_ == UINT64_MAX) {
            Trip(Fault::QueueFull); return false;
        }
        queue_[(head_ + count_) % Capacity] = {lease_.epoch, ++sequence_, event};
        ++count_; routed_ = true; return true;
    }
    void Trip(Fault reason) noexcept {
        if (active_) {
            resetRequired_ = resetRequired_ || routed_;
            active_ = false;
            for (auto& owner : owners_)
                if (owner == Owner::Target) owner = Owner::Draining;
        }
        for (auto& packet : queue_) packet = {};
        head_ = count_ = 0; fault_ = reason;
    }
    const std::uint64_t device_;
    Owner owners_[KeyCount + ButtonCount]{};
    Packet queue_[Capacity]{};
    Lease lease_{};
    std::uint64_t lastEpoch_ = 0, sequence_ = 0, lastNow_ = 0;
    std::size_t head_ = 0, count_ = 0;
    Fault fault_ = Fault::None;
    bool active_ = false, resetRequired_ = false, routed_ = false;
    bool removed_ = false, hasClock_ = false, clockRegressed_ = false;
};
} // namespace retsar::lab::input