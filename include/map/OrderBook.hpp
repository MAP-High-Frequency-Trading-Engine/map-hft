#pragma once
#include <memory_resource>
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

struct LevelInfo {
    Price price;
    Quantity totalQty;
};

class OrderBook {
public:

#ifdef MAP_USE_PMR
    using LevelQueue = std::pmr::deque<Order>;
    using PriceMapBids = std::pmr::map<Price, LevelQueue, std::greater<Price>>;
    using PriceMapAsks = std::pmr::map<Price, LevelQueue, std::less<Price>>;
    using IndexMap = std::pmr::map<OrderId, std::tuple<std::string, Side, Price>>;


#else
    using LevelQueue = std::deque<Order>;
    using PriceMapBids = std::map<Price, LevelQueue, std::greater<Price>>;
    using PriceMapAsks = std::map<Price, LevelQueue, std::less<Price>>;
    using IndexMap     = std::map<OrderId,
        std::tuple<std::string, Side, Price>>;
#endif

    struct PerSymbolBook {
#ifdef MAP_USE_PMR
        PriceMapBids bids;
        PriceMapAsks asks;

        PerSymbolBook(std::pmr::memory_resource* mr)
            : bids(mr), asks(mr) {}
#else
        PriceMapBids bids;
        PriceMapAsks asks;

        PerSymbolBook() = default;
#endif
    };

    explicit OrderBook(
        RiskLimits* risk = nullptr
#ifdef MAP_USE_PMR
        , std::pmr::memory_resource* mr = std::pmr::get_default_resource()
#endif
    );

    OrderId addOrder(Side side, Price px, Quantity qty,
                     const std::string& symbol = "TEST");

    bool cancelOrder(OrderId id);

    std::optional<Price> bestBid(const std::string& symbol) const;
    std::optional<Price> bestAsk(const std::string& symbol) const;

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

    std::vector<LevelInfo> snapshot(Side side) const;

    Quantity totalDepth(const std::string& symbol, Side side, int maxLevels=0) const;
    double orderImbalance(const std::string& symbol, int maxLevels=3) const;

    Quantity totalDepth(Side side, int maxLevels=0) const;
    double orderImbalance(int maxLevels=3) const;

    uint64_t checksum() const;


    // Branchless top-of-book selector: avoid if/else on Side.
    // Returns std::nullopt if both sides are empty for this symbol.
    std::optional<Price> bestPriceBranchless(Side side, const std::string& symbol) const {
        auto bidOpt = bestBid(symbol);
        auto askOpt = bestAsk(symbol);

        // If both sides are empty, we have no price at all.
        if (!bidOpt && !askOpt) {
            return std::nullopt;
        }

        // If only one side exists, just return that (still no Side branch).
        // Treat missing side as 0 ticks here.
        int bidTicks = bidOpt ? bidOpt->raw() : 0;
        int askTicks = askOpt ? askOpt->raw() : 0;

        // side == Bid ? 1 : 0  (as int)
        int isBid = (side == Side::Bid);

        // branchless selection: chosen = isBid * bid + (1-isBid) * ask
        int chosenTicks = isBid * bidTicks + (1 - isBid) * askTicks;

        // If chosenTicks is 0 (e.g. only ask side and side==Bid, or vice versa),
        // fall back to whichever side actually exists.
        if (chosenTicks == 0) {
            if (bidOpt) return bidOpt;
            if (askOpt) return askOpt;
            return std::nullopt; // ultra-defensive
        }

        return Price(chosenTicks);
    }

    // Optional: branchless mid calculation as another example.
    // Returns std::nullopt if we don't have both sides.
    std::optional<Price> midBranchless(const std::string& symbol) const {
        auto bidOpt = bestBid(symbol);
        auto askOpt = bestAsk(symbol);

        if (!bidOpt || !askOpt) {
            return std::nullopt;
        }

        int bidTicks = bidOpt->raw();
        int askTicks = askOpt->raw();

        // (bid + ask) / 2 using a shift for the average: bid + (ask - bid)/2
        int midTicks = bidTicks + ((askTicks - bidTicks) >> 1);
        return Price(midTicks);
    }


private:

    RiskLimits* risk_ = nullptr;

#ifdef MAP_USE_PMR
    std::pmr::monotonic_buffer_resource buffer_;
    std::pmr::map<std::string, PerSymbolBook> books_;
    IndexMap index_;
#else
    std::map<std::string, PerSymbolBook> books_;
    IndexMap index_;
#endif

    uint64_t nextId_ = 1;

    PerSymbolBook*       getOrCreateBook(const std::string& symbol);
    const PerSymbolBook* getBook(const std::string& symbol) const;

    const PerSymbolBook* getFirstBook() const;
    std::string          getFirstSymbol() const;
};

}
