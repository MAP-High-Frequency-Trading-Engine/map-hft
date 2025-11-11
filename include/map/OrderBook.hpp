#pragma once

#include <map>
#include <deque>
#include <optional>
#include <vector>
#include <cstdint>

#include "map/Types.hpp"
#include "map/Side.hpp"
#include "map/Order.hpp"

namespace map {

    // One row of the book for visualization / inspection
    struct LevelInfo {
        Price    price;
        Quantity totalQty;
    };

    class OrderBook {
    public:
        OrderBook();

        // Add a new limit order, return its assigned OrderId
        OrderId addOrder(Side side, Price px, Quantity qty);

        // Cancel an existing order by ID. Returns true if it was found & removed.
        bool cancelOrder(OrderId id);

        // Best prices on each side (nullopt if that side is empty)
        std::optional<Price> bestBid() const;
        std::optional<Price> bestAsk() const;

        // Snapshot of one side: sorted by price (bids: high→low, asks: low→high)
        std::vector<LevelInfo> snapshot(Side side) const;

        // Deterministic state checksum (for replay verification)
        std::uint64_t checksum() const;

    private:
        using LevelQueue = std::deque<Order>;

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
