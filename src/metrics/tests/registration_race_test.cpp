/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sisl/options/options.h>

#include "sisl/metrics/metrics.hpp"

SISL_LOGGING_INIT(vmod_metrics_framework)

RCU_REGISTER_INIT

using namespace sisl;

namespace {

// Shaped after sisl::StreamTrackerMetrics: one group instance per tracked object, so new instances keep
// registering while earlier ones are still being updated. group_impl_type_t::atomic keeps the value
// storage itself trivially race-free, so the only shared state under test is the Named* singletons.
class ReplicaMetrics : public MetricsGroup {
public:
    explicit ReplicaMetrics(const std::string& inst_name) :
            MetricsGroup("Replica", inst_name, group_impl_type_t::atomic) {
        REGISTER_COUNTER(replica_updates, "Updates applied to this replica");
        REGISTER_COUNTER(replica_conflicts, "Conflicting updates rejected");
        REGISTER_GAUGE(replica_queue_depth, "Current replication queue depth");
        REGISTER_HISTOGRAM(replica_apply_latency, "Update apply latency");

        register_me_to_farm();
    }
    ~ReplicaMetrics() { deregister_me_from_farm(); }
};

constexpr size_t REGISTRAR_THREADS{4};
constexpr size_t INSTANCES_PER_REGISTRAR{20};

} // namespace

// REGISTER_* writes the global NamedCounter/NamedGauge/NamedHistogram<name> singletons through
// set_index(), while COUNTER_INCREMENT and friends read them through get_index() under no lock. Building
// a second instance of a group on one thread while another thread updates an existing instance therefore
// races on those globals -- the shape craft_client hits when it builds a replica's route map. Every
// registration writes the same index, so the values stay correct; this asserts that invariant directly,
// and under TSAN it fails on the race itself.
TEST(MetricsRegistrationRace, ConcurrentRegistrationWhileUpdating) {
    ReplicaMetrics first{"replica_0"};

    const auto counter_idx{COUNTER_INDEX(replica_updates)};
    const auto gauge_idx{GAUGE_INDEX(replica_queue_depth)};
    const auto hist_idx{HISTOGRAM_INDEX(replica_apply_latency)};

    std::atomic< bool > stop{false};
    std::atomic< uint64_t > mismatches{0};

    std::thread updater{[&stop, &mismatches, &first, counter_idx, gauge_idx, hist_idx]() {
        while (!stop.load(std::memory_order_relaxed)) {
            COUNTER_INCREMENT(first, replica_updates, 1);
            GAUGE_UPDATE(first, replica_queue_depth, 1);
            HISTOGRAM_OBSERVE(first, replica_apply_latency, 1);

            if ((COUNTER_INDEX(replica_updates) != counter_idx) || (GAUGE_INDEX(replica_queue_depth) != gauge_idx) ||
                (HISTOGRAM_INDEX(replica_apply_latency) != hist_idx)) {
                mismatches.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }};

    // Each registrar owns its own slot in a pre-sized container, so the container is never contended.
    std::vector< std::vector< std::unique_ptr< ReplicaMetrics > > > held(REGISTRAR_THREADS);
    std::vector< std::thread > registrars;
    for (size_t t{0}; t < REGISTRAR_THREADS; ++t) {
        registrars.emplace_back([&held, t]() {
            for (size_t i{0}; i < INSTANCES_PER_REGISTRAR; ++i) {
                held[t].emplace_back(
                    std::make_unique< ReplicaMetrics >("replica_" + std::to_string(t) + "_" + std::to_string(i)));
            }
        });
    }

    for (auto& r : registrars) {
        r.join();
    }
    stop.store(true, std::memory_order_relaxed);
    updater.join();

    // Instances are destroyed only after every join, so deregistration cannot pollute the TSAN report.
    EXPECT_EQ(mismatches.load(std::memory_order_relaxed), 0u);
    EXPECT_GT(COUNTER_VALUE(first, replica_updates), 0);
}

SISL_OPTIONS_ENABLE(logging)
int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
