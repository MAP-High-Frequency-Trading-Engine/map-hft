#pragma once

#include <string>
#include <random>
#include <cstdint>
#include <unordered_map>

#include "map/StrongHash.hpp"      // for std::unordered_map<Strong<...>>
#include "map/Types.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/Side.hpp"

namespace map {

    class OrderBook;   // forward declaration

    class BasicStrategy {
    public:
        struct Params {
            Price        basePrice{100};
            Quantity     clipSize{10};
            std::uint64_t ticksPerOrder{1};   // base spacing between orders

            // Week 3: aggressiveness + bounds
            int aggressivenessMode{1};        // 0 = slow, 1 = medium, 2 = aggressive

            Quantity     minClip{Quantity{1}};
            Quantity     maxClip{Quantity{50}};
            std::uint64_t minTicksPerOrder{1};
            std::uint64_t maxTicksPerOrder{10};
        };

        // live_sim expects this exact signature:
        // BasicStrategy strat(bus, symbol, params);
        BasicStrategy(EventBus& bus,
                      const std::string& symbol,
                      const Params& params);

        // Called each simulation tick
        void onTick(std::uint64_t t, OrderBook& book);

    private:
        void fireOne();
        void updateIntent(const OrderBook& book);

        EventBus&    bus_;
        std::string  symbol_;
        Price        basePrice_;
        Quantity     clipSize_;          // baseline size (for defaults)
        std::uint64_t ticksPerOrder_;    // baseline ticks between orders

        // --- Intent / aggressiveness state (Week 3) ---
        int            aggressivenessMode_;
        Quantity       minClip_;
        Quantity       maxClip_;
        std::uint64_t  minTicksPerOrder_;
        std::uint64_t  maxTicksPerOrder_;

        Quantity       currentClip_;
        std::uint64_t  currentTicksPerOrder_;
        Side           biasSide_;

        // Track outstanding orders (not really used yet, but kept around)
        std::unordered_map<OrderId, Price> active_orders_;

        // RNG for side + price noise
        std::mt19937                        rng_;
        std::uniform_int_distribution<int>  sideDist_;   // returns 0 or 1
        std::normal_distribution<double>    priceNoise_; // mean 0, stddev 1
    };

} // namespace map
