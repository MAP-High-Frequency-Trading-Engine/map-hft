#pragma once

#include <chrono>
#include <random>
#include <thread>
#include <algorithm>

namespace map {

    struct NetJitterConfig {
        // Base one-way latency in microseconds (e.g. 50 µs)
        int baseMicros      = 50;
        // Peak absolute jitter (± jitterMicros)
        int jitterMicros    = 20;
        // Whether to enable the model at all
        bool enabled        = true;
    };

    // Sleep for baseMicros ± jitterMicros (clamped to >= 0)
    template <typename RNG>
    inline void simulate_network_delay(const NetJitterConfig& cfg, RNG& rng) {
        if (!cfg.enabled || (cfg.baseMicros <= 0 && cfg.jitterMicros <= 0)) {
            return;
        }

        std::uniform_int_distribution<int> jitterDist(
            -std::max(0, cfg.jitterMicros),
             std::max(0, cfg.jitterMicros)
        );

        int total = cfg.baseMicros + jitterDist(rng);
        if (total <= 0) return;

        std::this_thread::sleep_for(std::chrono::microseconds(total));
    }

} // namespace map
