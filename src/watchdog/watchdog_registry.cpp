#include <sisl/watchdog/watchdog_registry.hpp>

namespace sisl {

// Watchdog base

Watchdog::~Watchdog() {
    if (registry_ == nullptr) { return; }
    registry_->remove(id_);
    registry_ = nullptr;
    id_ = 0;
}

// DeadlineWatchdog

DeadlineWatchdog::DeadlineWatchdog(std::string name, std::chrono::milliseconds limit) :
        name_(std::move(name)), limit_(limit) {
    anchor.store(std::chrono::steady_clock::now());
}

std::optional< WatchdogFailure > DeadlineWatchdog::check(std::chrono::steady_clock::time_point now) const {
    auto const a = anchor.load();
    if (now <= a + limit_) { return std::nullopt; }
    auto const elapsed_ms = std::chrono::duration_cast< std::chrono::milliseconds >(now - a).count();
    auto const limit_ms = std::chrono::duration_cast< std::chrono::milliseconds >(limit_).count();
    auto const details =
        "last seen " + std::to_string(elapsed_ms) + "ms ago (limit=" + std::to_string(limit_ms) + "ms)";
    return WatchdogFailure{name_, details, WatchdogSeverity::critical};
}

// LeaseWatchdog

void LeaseWatchdog::kick() { anchor.store(std::chrono::steady_clock::now()); }

// BarkWatchdog

BarkWatchdog::BarkWatchdog(std::string name, std::string details) :
        name_(std::move(name)), details_(std::move(details)) {}

std::optional< WatchdogFailure > BarkWatchdog::check(std::chrono::steady_clock::time_point) const {
    return WatchdogFailure{name_, details_, WatchdogSeverity::critical};
}

// WatchdogRegistry

uint64_t WatchdogRegistry::add(Watchdog* dog) {
    std::lock_guard< std::mutex > lock(mu_);
    auto const id = next_id_++;
    dog->registry_ = this;
    dog->id_ = id;
    dogs_.emplace(id, dog);
    return id;
}

void WatchdogRegistry::remove(uint64_t id) {
    std::lock_guard< std::mutex > lock(mu_);
    dogs_.erase(id);
}

std::unique_ptr< DeadlineWatchdog > WatchdogRegistry::add_deadline(std::string name, std::chrono::milliseconds limit) {
    auto dog = std::make_unique< DeadlineWatchdog >(std::move(name), limit);
    add(dog.get());
    return dog;
}

std::unique_ptr< LeaseWatchdog > WatchdogRegistry::add_lease(std::string name, std::chrono::milliseconds limit) {
    auto dog = std::make_unique< LeaseWatchdog >(std::move(name), limit);
    add(dog.get());
    return dog;
}

std::unique_ptr< BarkWatchdog > WatchdogRegistry::add_bark(std::string name, std::string details) {
    auto dog = std::make_unique< BarkWatchdog >(std::move(name), std::move(details));
    add(dog.get());
    return dog;
}

std::vector< WatchdogFailure > WatchdogRegistry::failures() const {
    std::vector< WatchdogFailure > out;
    auto const now = std::chrono::steady_clock::now();
    std::lock_guard< std::mutex > lock(mu_);
    out.reserve(dogs_.size());
    for (auto const& entry : dogs_) {
        if (auto f = entry.second->check(now)) { out.push_back(std::move(*f)); }
    }
    return out;
}

} // namespace sisl
