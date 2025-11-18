#pragma once

#include <map>
#include <deque>
#include <optional>
#include <vector>
#include <cstdint>
#include <string>

#include "map/Types.hpp"
#include "map/Side.hpp"
#include "map/Order.hpp"
#include "map/risk/RiskLimits.hpp"

namespace map {

    // One row of the book for visualization / inspection
    struct LevelInfo {
        Price    price;
        Quantity totalQty;
    };

    class OrderBook {
    public:
        explicit OrderBook(RiskLimits* risk = nullptr)
            : risk_(risk)
        {}

        void setRiskLimits(RiskLimits* risk) { risk_ = risk; }

        OrderId addOrder(Side side,
                         Price px,
                         Quantity qty,
                         const std::string& symbol = "TEST");

        bool cancelOrder(OrderId id);

        std::optional<Price> bestBid() const;
        std::optional<Price> bestAsk() const;

        std::vector<LevelInfo> snapshot(Side side) const;

        // NEW:
        Quantity totalDepth(Side side, int maxLevels = 0) const;
        double   orderImbalance(int maxLevels = 3) const;

        std::uint64_t checksum() const;



    private:
        using LevelQueue = std::deque<Order>;

        RiskLimits* risk_ = nullptr;  // non-owning, can be null

        // bids_: highest price first
        std::map<Price, LevelQueue, std::greater<Price>> bids_;
        // asks_: lowest price first
        std::map<Price, LevelQueue, std::less<Price>>    asks_;

        // Index: OrderId → (Side, Price) so we can locate + cancel efficiently
        std::map<OrderId, std::pair<Side, Price>>        index_;

        // Monotonic id generator
        std::uint64_t                                    nextId_ = 1;
    };

} // namespace map
