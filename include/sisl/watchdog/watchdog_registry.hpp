#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sisl {

enum class WatchdogSeverity {
    critical,
};

struct WatchdogFailure {
    std::string name;
    std::string details;
    WatchdogSeverity severity;
};

class WatchdogRegistry;

// Abstract base for all watchdog types. Non-copyable, non-movable.
// Destructor automatically deregisters from the registry.
class Watchdog {
public:
    Watchdog() = default;
    virtual ~Watchdog();
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) = delete;
    Watchdog& operator=(Watchdog&&) = delete;

    // check() must NOT call back into the owning WatchdogRegistry (failures()/add()/remove()), directly or
    // indirectly, from the same thread: WatchdogRegistry::mu_ is a plain (non-recursive) std::mutex, so a
    // same-thread reentrant call deadlocks. failures() asserts against this in debug builds.
    virtual std::optional< WatchdogFailure > check(std::chrono::steady_clock::time_point now) const = 0;

protected:
    // Removes this watchdog from its registry, if still registered. Idempotent. Every concrete (leaf) subclass
    // that owns data read by check() must call this as the FIRST statement of its own destructor -- the base
    // ~Watchdog() runs last, after derived members are already destroyed, which is too late: a concurrent
    // WatchdogRegistry::failures() could still find this object in the registry and call check() on it,
    // reading already-destroyed members (e.g. DeadlineWatchdog::name_). Calling it early, under the same
    // mutex failures() holds, guarantees failures() never observes a partially-destroyed watchdog.
    void detach_from_registry();

private:
    friend class WatchdogRegistry;
    WatchdogRegistry* registry_{nullptr};
    uint64_t id_{0};
};

// Watches a single operation that must complete within a fixed deadline.
class DeadlineWatchdog : public Watchdog {
public:
    DeadlineWatchdog(std::string name, std::chrono::milliseconds limit);
    ~DeadlineWatchdog() override;
    std::optional< WatchdogFailure > check(std::chrono::steady_clock::time_point now) const override;

protected:
    std::atomic< std::chrono::steady_clock::time_point > anchor{};

private:
    std::string name_;
    std::chrono::milliseconds limit_{0};
};

// For a recurring loop that must keep making progress. Call kick() each iteration to extend the deadline.
class LeaseWatchdog : public DeadlineWatchdog {
public:
    LeaseWatchdog(std::string name, std::chrono::milliseconds limit) : DeadlineWatchdog(std::move(name), limit) {}
    void kick();
};

// For signaling that the system is already in a known-bad state. Fails immediately on construction.
class BarkWatchdog : public Watchdog {
public:
    BarkWatchdog(std::string name, std::string details);
    ~BarkWatchdog() override;
    std::optional< WatchdogFailure > check(std::chrono::steady_clock::time_point now) const override;

private:
    std::string name_;
    std::string details_;
};

class WatchdogRegistry {
public:
    WatchdogRegistry() = default;
    ~WatchdogRegistry();

    static WatchdogRegistry& instance() {
        static WatchdogRegistry s_instance;
        return s_instance;
    }

    std::unique_ptr< DeadlineWatchdog > add_deadline(std::string name, std::chrono::milliseconds limit);
    std::unique_ptr< LeaseWatchdog > add_lease(std::string name, std::chrono::milliseconds limit);
    std::unique_ptr< BarkWatchdog > add_bark(std::string name, std::string details = {});

    std::vector< WatchdogFailure > failures() const;

private:
    friend class Watchdog;

    uint64_t add(Watchdog* dog);
    void remove(uint64_t id);

    mutable std::mutex mu_;
    mutable std::atomic< std::thread::id > checking_thread_{}; // set while failures() runs; reentrancy guard
    uint64_t next_id_{1};
    std::unordered_map< uint64_t, Watchdog* > dogs_; // non-owning; caller owns via unique_ptr
};

} // namespace sisl
