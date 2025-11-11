#pragma once

#include <string>
#include <random>
#include <cstdint>

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
            std::uint64_t ticksPerOrder{1};   // how many ticks between orders
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

        EventBus&    bus_;
        std::string  symbol_;
        Price        basePrice_;
        Quantity     clipSize_;
        std::uint64_t ticksPerOrder_;

        std::mt19937                      rng_;
        std::uniform_int_distribution<int> sideDist_;
        std::normal_distribution<double>   priceNoise_;
    };

} // namespace map
