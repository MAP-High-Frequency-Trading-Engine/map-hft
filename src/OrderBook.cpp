#include "map/OrderBook.hpp"

#include <iostream>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace map {

    // --- ctor ---
    OrderBook::OrderBook(RiskLimits* risk)
        : risk_(risk)
    {
    }

    // --- Helpers ---

    OrderBook::PerSymbolBook* OrderBook::getOrCreateBook(const std::string& symbol) {
        auto it = books_.find(symbol);
        if (it == books_.end()) {
            it = books_.emplace(symbol, PerSymbolBook{}).first;
        }
        return &it->second;
    }

    const OrderBook::PerSymbolBook* OrderBook::getBook(const std::string& symbol) const {
        auto it = books_.find(symbol);
        if (it == books_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const OrderBook::PerSymbolBook* OrderBook::getFirstBook() const {
        if (books_.empty()) return nullptr;
        return &books_.begin()->second;
    }

    std::string OrderBook::getFirstSymbol() const {
        if (books_.empty()) return {};
        return books_.begin()->first;
    }

    // --- addOrder (multi-symbol) ---
    OrderId OrderBook::addOrder(Side side,
                                Price px,
                                Quantity qty,
                                const std::string& symbol)
    {
        // 1. Sanity check
        if (qty.raw() <= 0 || px.raw() <= 0) {
            std::cerr << "ERR: Invalid qty or px for new order." << std::endl;
            return OrderId{0};
        }

        // 2. Optional risk check (still disabled by default)
        // if (risk_ && !risk_->checkOrder(symbol, side, px, qty)) {
        //     std::cerr << "RISK REJECT for new order\n";
        //     return OrderId{0};
        // }

        // 3. Generate a new OrderId
        OrderId newId = OrderId{nextId_++};

        // 4. Get per-symbol book
        PerSymbolBook* book = getOrCreateBook(symbol);

        // Remaining qty to be matched + potentially rested
        Quantity remaining = qty;

        // Matching lambda uses the *symbol-specific* book
        auto matchAgainst = [&](auto& aggressorRemaining,
                                Side aggressorSide,
                                Price aggressorPx) {
            if (aggressorSide == Side::Bid) {
                // Match vs asks_ for THIS symbol
                while (aggressorRemaining.raw() > 0 && !book->asks.empty()) {
                    auto bestAskIt = book->asks.begin(); // lowest ask
                    Price bestAskPx = bestAskIt->first;
                    if (bestAskPx.raw() > aggressorPx.raw()) {
                        break; // no more crossing prices
                    }

                    auto& queue = bestAskIt->second;
                    while (aggressorRemaining.raw() > 0 && !queue.empty()) {
                        Order& resting = queue.front();
                        Quantity restingQty = resting.qty;

                        if (restingQty.raw() <= aggressorRemaining.raw()) {
                            // Full fill of resting order
                            aggressorRemaining =
                                Quantity{aggressorRemaining.raw() - restingQty.raw()};

                            // Remove from index and queue
                            index_.erase(resting.id);
                            queue.pop_front();
                        } else {
                            // Partial fill of resting order
                            resting.qty = Quantity{
                                restingQty.raw() - aggressorRemaining.raw()
                            };
                            aggressorRemaining = Quantity{0};
                        }
                    }

                    if (queue.empty()) {
                        book->asks.erase(bestAskIt);
                    }
                }
            } else { // Ask side
                while (aggressorRemaining.raw() > 0 && !book->bids.empty()) {
                    auto bestBidIt = book->bids.begin(); // highest bid
                    Price bestBidPx = bestBidIt->first;
                    if (bestBidPx.raw() < aggressorPx.raw()) {
                        break; // no more crossing prices
                    }

                    auto& queue = bestBidIt->second;
                    while (aggressorRemaining.raw() > 0 && !queue.empty()) {
                        Order& resting = queue.front();
                        Quantity restingQty = resting.qty;

                        if (restingQty.raw() <= aggressorRemaining.raw()) {
                            // Full fill
                            aggressorRemaining =
                                Quantity{aggressorRemaining.raw() - restingQty.raw()};

                            index_.erase(resting.id);
                            queue.pop_front();
                        } else {
                            // Partial fill
                            resting.qty = Quantity{
                                restingQty.raw() - aggressorRemaining.raw()
                            };
                            aggressorRemaining = Quantity{0};
                        }
                    }

                    if (queue.empty()) {
                        book->bids.erase(bestBidIt);
                    }
                }
            }
        };

        // 5. Do the matching pass in this symbol's book
        matchAgainst(remaining, side, px);

        // 6. If anything remains, rest it in this symbol's book
        if (remaining.raw() > 0) {
            Order resting{newId, symbol, side, px, remaining};

            LevelQueue& levelQueue =
                (side == Side::Bid) ? book->bids[px] : book->asks[px];

            levelQueue.push_back(resting);
            index_[newId] = std::make_tuple(symbol, side, px);
        }

        return newId;
    }

    // --- cancelOrder (multi-symbol) ---
    bool OrderBook::cancelOrder(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) {
            return false; // Not found
        }

        const std::string& symbol = std::get<0>(it->second);
        Side               side   = std::get<1>(it->second);
        Price              px     = std::get<2>(it->second);

        PerSymbolBook* book = getOrCreateBook(symbol);

        if (side == Side::Bid) {
            auto levelIt = book->bids.find(px);
            if (levelIt != book->bids.end()) {
                auto& queue = levelIt->second;
                auto orderIt = std::find_if(
                    queue.begin(), queue.end(),
                    [&id](const Order& o) { return o.id.raw() == id.raw(); });

                if (orderIt != queue.end()) {
                    queue.erase(orderIt);
                    if (queue.empty()) {
                        book->bids.erase(levelIt);
                    }
                }
            }
        } else { // Side::Ask
            auto levelIt = book->asks.find(px);
            if (levelIt != book->asks.end()) {
                auto& queue = levelIt->second;
                auto orderIt = std::find_if(
                    queue.begin(), queue.end(),
                    [&id](const Order& o) { return o.id.raw() == id.raw(); });

                if (orderIt != queue.end()) {
                    queue.erase(orderIt);
                    if (queue.empty()) {
                        book->asks.erase(levelIt);
                    }
                }
            }
        }

        index_.erase(it);
        return true;
    }

    // --- bestBid / bestAsk (symbol-specific) ---

    std::optional<Price> OrderBook::bestBid(const std::string& symbol) const {
        const PerSymbolBook* book = getBook(symbol);
        if (!book || book->bids.empty()) return std::nullopt;
        return book->bids.begin()->first;
    }

    std::optional<Price> OrderBook::bestAsk(const std::string& symbol) const {
        const PerSymbolBook* book = getBook(symbol);
        if (!book || book->asks.empty()) return std::nullopt;
        return book->asks.begin()->first;
    }

    // --- Legacy bestBid / bestAsk (use "first" symbol) ---

    std::optional<Price> OrderBook::bestBid() const {
        const PerSymbolBook* book = getFirstBook();
        if (!book || book->bids.empty()) return std::nullopt;
        return book->bids.begin()->first;
    }

    std::optional<Price> OrderBook::bestAsk() const {
        const PerSymbolBook* book = getFirstBook();
        if (!book || book->asks.empty()) return std::nullopt;
        return book->asks.begin()->first;
    }

    // --- snapshot (first symbol) ---
    std::vector<LevelInfo> OrderBook::snapshot(Side side) const {
        std::vector<LevelInfo> levels;
        const PerSymbolBook* book = getFirstBook();
        if (!book) return levels;

        if (side == Side::Bid) {
            for (const auto& [price, queue] : book->bids) {
                Quantity totalQty{0};
                for (const auto& order : queue) {
                    totalQty = Quantity{ totalQty.raw() + order.qty.raw() };
                }
                levels.push_back({price, totalQty});
            }
        } else {
            for (const auto& [price, queue] : book->asks) {
                Quantity totalQty{0};
                for (const auto& order : queue) {
                    totalQty = Quantity{ totalQty.raw() + order.qty.raw() };
                }
                levels.push_back({price, totalQty});
            }
        }

        return levels;
    }

    // --- totalDepth (symbol-specific) ---
    Quantity OrderBook::totalDepth(const std::string& symbol,
                                   Side side,
                                   int maxLevels) const
    {
        const PerSymbolBook* book = getBook(symbol);
        if (!book) return Quantity{0};

        Quantity total{0};
        int levelCount = 0;

        if (side == Side::Bid) {
            for (const auto& [price, queue] : book->bids) {
                (void)price;
                if (maxLevels > 0 && levelCount >= maxLevels) break;
                for (const auto& order : queue) {
                    total = Quantity{ total.raw() + order.qty.raw() };
                }
                ++levelCount;
            }
        } else {
            for (const auto& [price, queue] : book->asks) {
                (void)price;
                if (maxLevels > 0 && levelCount >= maxLevels) break;
                for (const auto& order : queue) {
                    total = Quantity{ total.raw() + order.qty.raw() };
                }
                ++levelCount;
            }
        }

        return total;
    }

    // --- orderImbalance (symbol-specific) ---
    double OrderBook::orderImbalance(const std::string& symbol,
                                     int maxLevels) const
    {
        Quantity bidDepth = totalDepth(symbol, Side::Bid, maxLevels);
        Quantity askDepth = totalDepth(symbol, Side::Ask, maxLevels);

        double b = static_cast<double>(bidDepth.raw());
        double a = static_cast<double>(askDepth.raw());

        double denom = b + a;
        if (denom == 0.0) {
            return 0.0; // no liquidity on either side
        }
        return (b - a) / denom; // -1 (all asks) to +1 (all bids)
    }

    // --- Legacy totalDepth / orderImbalance (first symbol) ---

    Quantity OrderBook::totalDepth(Side side, int maxLevels) const {
        std::string symbol = getFirstSymbol();
        if (symbol.empty()) return Quantity{0};
        return totalDepth(symbol, side, maxLevels);
    }

    double OrderBook::orderImbalance(int maxLevels) const {
        std::string symbol = getFirstSymbol();
        if (symbol.empty()) return 0.0;
        return orderImbalance(symbol, maxLevels);
    }

    // --- checksum ---
    std::uint64_t OrderBook::checksum() const {
        std::uint64_t hash = 0;

        for (const auto& [symbol, book] : books_) {
            // Mix in the symbol name
            std::uint64_t symHash = std::hash<std::string>{}(symbol);
            hash ^= symHash;

            // Bids checksum
            for (const auto& [price, queue] : book.bids) {
                hash ^= static_cast<std::uint64_t>(price.raw());
                for (const auto& order : queue) {
                    hash += order.id.raw();
                    hash += static_cast<std::uint64_t>(order.qty.raw());
                }
            }

            // Asks checksum
            for (const auto& [price, queue] : book.asks) {
                hash ^= static_cast<std::uint64_t>(price.raw());
                for (const auto& order : queue) {
                    hash += order.id.raw();
                    hash += static_cast<std::uint64_t>(order.qty.raw());
                }
            }
        }

        return hash;
    }

} // namespace map
