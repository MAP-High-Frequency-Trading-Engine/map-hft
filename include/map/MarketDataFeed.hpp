#pragma once

#include "map/Types.hpp"
#include "map/OrderBook.hpp"
#include <optional>
#include <string>

namespace map {

    /**
     * @brief Read-only view over a single OrderBook, plus a getBook() shim.
     *
     * Right now we only have one OrderBook instance; getBook(symbol) just returns that.
     * Later you can extend this to a map<string, OrderBook>.
     */
    class MarketDataFeed {
    public:
        explicit MarketDataFeed(OrderBook& book)
            : book_(book) {}

        // Returns the current best bid price.
        std::optional<Price> getBestBid() const {
            return book_.bestBid();
        }

        // Returns the current best ask price.
        std::optional<Price> getBestAsk() const {
            return book_.bestAsk();
        }

        // Shim to match live_sim.cpp usage: ignores symbol for now.
        OrderBook& getBook(const std::string& /*symbol*/) {
            return book_;
        }

        const OrderBook& getBook(const std::string& /*symbol*/) const {
            return book_;
        }

    private:
        OrderBook& book_;
    };

} // namespace map
