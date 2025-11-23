#pragma once

#include <map>
#include <deque>
#include <optional>
#include <vector>
#include <cstdint>
#include <string>
#include <tuple>

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
        explicit OrderBook(RiskLimits* risk = nullptr);

        void setRiskLimits(RiskLimits* risk) { risk_ = risk; }

        // Multi-symbol addOrder: symbol is required to route to the right book
        OrderId addOrder(Side side,
                         Price px,
                         Quantity qty,
                         const std::string& symbol = "TEST");

        bool cancelOrder(OrderId id);

        // Legacy, symbol-agnostic best bid/ask:
        // In a single-symbol run, this just uses the first symbol's book.
        std::optional<Price> bestBid() const;
        std::optional<Price> bestAsk() const;

        // Symbol-specific best bid/ask
        std::optional<Price> bestBid(const std::string& symbol) const;
        std::optional<Price> bestAsk(const std::string& symbol) const;

        // Snapshot for whichever symbol is "first" (useful when you only run one)
        std::vector<LevelInfo> snapshot(Side side) const;

        // Symbol-specific depth & imbalance
        Quantity totalDepth(const std::string& symbol,
                            Side side,
                            int maxLevels = 0) const;

        double orderImbalance(const std::string& symbol,
                              int maxLevels = 3) const;

        // Legacy symbol-agnostic interfaces: operate on the "first" symbol
        Quantity totalDepth(Side side, int maxLevels = 0) const;
        double   orderImbalance(int maxLevels = 3) const;

        // Global checksum across all symbols
        std::uint64_t checksum() const;

    private:
        struct PerSymbolBook;
        using LevelQueue = std::deque<Order>;

        struct PerSymbolBook {
            // bids_: highest price first
            std::map<Price, LevelQueue, std::greater<Price>> bids;
            // asks_: lowest price first
            std::map<Price, LevelQueue, std::less<Price>>    asks;
        };

        RiskLimits* risk_ = nullptr;  // non-owning, can be null

        // Symbol → its own book (bids/asks)
        std::map<std::string, PerSymbolBook> books_;

        // Index: OrderId → (symbol, Side, Price)
        std::map<OrderId, std::tuple<std::string, Side, Price>> index_;

        // Monotonic id generator (global across all symbols)
        std::uint64_t nextId_ = 1;

        // Helpers
        PerSymbolBook*       getOrCreateBook(const std::string& symbol);
        const PerSymbolBook* getBook(const std::string& symbol) const;
        const PerSymbolBook* getFirstBook() const;
        std::string          getFirstSymbol() const;
    };

} // namespace map
