#include "map/OrderBook.hpp"

#include <iostream>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace map {

    // --- OrderBook::addOrder ---
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

        // 2. Optional risk check (currently disabled)
        // if (risk_ && !risk_->checkOrder(symbol, side, px, qty)) {
        //     std::cerr << "RISK REJECT for new order\n";
        //     return OrderId{0};
        // }

        // 3. Generate a new OrderId
        OrderId newId = OrderId{nextId_++};

        // Remaining qty to be (possibly) matched + rested
        Quantity remaining = qty;

        // 4. Simple price-time matching
        auto matchAgainst = [&](auto& aggressorRemaining,
                                Side aggressorSide,
                                Price aggressorPx) {
            // For Bid: match vs asks_ (lowest ask first, price <= bid px)
            // For Ask: match vs bids_ (highest bid first, price >= ask px)
            if (aggressorSide == Side::Bid) {
                // match against asks_
                while (aggressorRemaining.raw() > 0 && !asks_.empty()) {
                    auto bestAskIt = asks_.begin(); // lowest ask
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
                        asks_.erase(bestAskIt);
                    }
                }
            } else {
                // aggressorSide == Ask → match against bids_
                while (aggressorRemaining.raw() > 0 && !bids_.empty()) {
                    auto bestBidIt = bids_.begin(); // highest bid
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
                        bids_.erase(bestBidIt);
                    }
                }
            }
        };

        // Do the matching pass
        matchAgainst(remaining, side, px);

        // 5. If anything remains, rest it in the book
        if (remaining.raw() > 0) {
            Order resting{newId, symbol, side, px, remaining};

            LevelQueue& levelQueue =
                (side == Side::Bid) ? bids_[px] : asks_[px];
            levelQueue.push_back(resting);
            index_[newId] = {side, px};
        } else {
            // If fully filled, we don't actually rest the order in the book.
            // The OrderId is still returned (could be used to log a trade).
        }

        return newId;
    }

    // --- OrderBook::cancelOrder ---
    bool OrderBook::cancelOrder(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) {
            return false; // Not found
        }

        Side  side = it->second.first;
        Price px   = it->second.second;

        if (side == Side::Bid) {
            auto levelIt = bids_.find(px);
            if (levelIt != bids_.end()) {
                auto& queue = levelIt->second;
                auto orderIt = std::find_if(
                    queue.begin(), queue.end(),
                    [&id](const Order& o) { return o.id.raw() == id.raw(); });

                if (orderIt != queue.end()) {
                    queue.erase(orderIt);
                    if (queue.empty()) {
                        bids_.erase(levelIt);
                    }
                }
            }
        } else { // Side::Ask
            auto levelIt = asks_.find(px);
            if (levelIt != asks_.end()) {
                auto& queue = levelIt->second;
                auto orderIt = std::find_if(
                    queue.begin(), queue.end(),
                    [&id](const Order& o) { return o.id.raw() == id.raw(); });

                if (orderIt != queue.end()) {
                    queue.erase(orderIt);
                    if (queue.empty()) {
                        asks_.erase(levelIt);
                    }
                }
            }
        }

        index_.erase(it);
        return true;
    }

    // --- OrderBook::bestBid / bestAsk ---
    std::optional<Price> OrderBook::bestBid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }

    std::optional<Price> OrderBook::bestAsk() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    // --- OrderBook::snapshot ---
    std::vector<LevelInfo> OrderBook::snapshot(Side side) const {
        std::vector<LevelInfo> levels;

        if (side == Side::Bid) {
            for (const auto& [price, queue] : bids_) {
                Quantity totalQty{0};
                for (const auto& order : queue) {
                    totalQty = Quantity{ totalQty.raw() + order.qty.raw() };
                }
                levels.push_back({price, totalQty});
            }
        } else {
            for (const auto& [price, queue] : asks_) {
                Quantity totalQty{0};
                for (const auto& order : queue) {
                    totalQty = Quantity{ totalQty.raw() + order.qty.raw() };
                }
                levels.push_back({price, totalQty});
            }
        }

        return levels;
    }

    // --- NEW: totalDepth (used by intent engine) ---
    Quantity OrderBook::totalDepth(Side side, int maxLevels) const {
        Quantity total{0};
        int levelCount = 0;

        if (side == Side::Bid) {
            for (const auto& [price, queue] : bids_) {
                (void)price;
                if (maxLevels > 0 && levelCount >= maxLevels) break;

                for (const auto& order : queue) {
                    total = Quantity{ total.raw() + order.qty.raw() };
                }
                ++levelCount;
            }
        } else {
            for (const auto& [price, queue] : asks_) {
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

    // --- NEW: orderImbalance ([-1, 1]) ---
    double OrderBook::orderImbalance(int maxLevels) const {
        Quantity bidDepth = totalDepth(Side::Bid, maxLevels);
        Quantity askDepth = totalDepth(Side::Ask, maxLevels);

        double b = static_cast<double>(bidDepth.raw());
        double a = static_cast<double>(askDepth.raw());

        double denom = b + a;
        if (denom == 0.0) {
            return 0.0; // no liquidity on either side
        }
        return (b - a) / denom; // -1 (all asks) to +1 (all bids)
    }

    // --- OrderBook::checksum ---
    std::uint64_t OrderBook::checksum() const {
        std::uint64_t hash = 0;

        // Bids checksum
        for (const auto& [price, queue] : bids_) {
            hash ^= static_cast<std::uint64_t>(price.raw());
            for (const auto& order : queue) {
                hash += order.id.raw();
                hash += static_cast<std::uint64_t>(order.qty.raw());
            }
        }

        // Asks checksum
        for (const auto& [price, queue] : asks_) {
            hash ^= static_cast<std::uint64_t>(price.raw());
            for (const auto& order : queue) {
                hash += order.id.raw();
                hash += static_cast<std::uint64_t>(order.qty.raw());
            }
        }

        return hash;
    }

} // namespace map
