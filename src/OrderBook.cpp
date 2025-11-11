// src/OrderBook.cpp

#include "map/OrderBook.hpp"
#include <algorithm> // std::min
#include <map>
#include "map/Types.hpp"
#include "map/Side.hpp"
#include "map/Order.hpp"
#include "map/core/RiskLimits.hpp"

namespace map {

// -------------------------
// Helper: generic matcher
// -------------------------
template <typename MySideMap, typename OppSideMap>
static void matchIncomingOrder(
    Order& incoming,
    Price limitPrice,
    Side side,
    MySideMap& mySide,
    OppSideMap& oppSide,
    std::map<OrderId, std::pair<Side, Price>>& index
) {
    // Try to match against the opposite side while:
    //  - incoming still has remaining quantity
    //  - there are resting orders on the opposite side
    while (incoming.remaining.raw() > 0 && !oppSide.empty()) {
        // Best price level on the opposite side
        auto bestIt    = oppSide.begin();
        Price bestPrice = bestIt->first;

        // Check if prices cross
        bool crosses = (side == Side::Bid)
            ? (bestPrice <= limitPrice)   // bid crosses ask
            : (bestPrice >= limitPrice);  // ask crosses bid

        if (!crosses) {
            break; // cannot trade further at this price
        }

        auto& queue = bestIt->second; // LevelQueue& (std::deque<Order>)

        // Match against orders in FIFO order at this price
        while (!queue.empty() && incoming.remaining.raw() > 0) {
            Order& resting = queue.front();

            auto tradedRaw = std::min(
                incoming.remaining.raw(),
                resting.remaining.raw()
            );

            incoming.remaining = Quantity{incoming.remaining.raw() - tradedRaw};
            resting.remaining  = Quantity{resting.remaining.raw()  - tradedRaw};

            // (Later) you can emit Trade events here, using tradedRaw and bestPrice

            if (resting.remaining.raw() == 0) {
                // Fully filled resting order: remove from index + queue
                index.erase(resting.id);
                queue.pop_front();
            } else {
                // Partially filled resting order stays at front
                break;
            }
        }

        // If that price level is now empty, remove the level
        if (queue.empty()) {
            oppSide.erase(bestIt);
        }
    }

    // If any quantity remains, rest it on "my side" at limitPrice
    if (incoming.remaining.raw() > 0) {
        auto& levelQueue = mySide[limitPrice];
        levelQueue.push_back(incoming);
        index[incoming.id] = { side, limitPrice };
    }
}

// -------------------------
// OrderBook methods
// -------------------------

OrderBook::OrderBook() = default;


    std::uint64_t OrderBook::checksum() const {
        std::uint64_t sum = 0;

        auto foldSide = [&](auto const& sideMap, std::uint64_t salt) {
            for (const auto& [price, queue] : sideMap) {
                std::int64_t levelQty = 0;
                for (const auto& o : queue) {
                    levelQty += o.remaining.raw();
                }
                sum ^= static_cast<std::uint64_t>(price.raw()) * salt
                     ^ static_cast<std::uint64_t>(levelQty);
            }
        };

        foldSide(bids_, 131);
        foldSide(asks_, 137);
        return sum;
    }

    OrderId OrderBook::addOrder(Side side, Price px, Quantity qty) {
        // basic sanity checks / risk limits
        if (qty.raw() <= 0) {
            throw std::invalid_argument("Order quantity must be positive");
        }
        if (qty.raw() > RiskLimits::maxOrderQty().raw()) {
            throw std::runtime_error("Order exceeds max allowed quantity");
        }
        if (px.raw() < RiskLimits::minPrice().raw() ||
            px.raw() > RiskLimits::maxPrice().raw()) {
            throw std::runtime_error("Price out of allowed range");
            }

        Order incoming{ OrderId{nextId_++}, side, px, qty };

        if (side == Side::Bid) {
            matchIncomingOrder(incoming, px, side, bids_, asks_, index_);
        } else {
            matchIncomingOrder(incoming, px, side, asks_, bids_, index_);
        }

        return incoming.id;
    }

bool OrderBook::cancelOrder(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) {
        return false; // no such order
    }

    auto [side, px] = it->second;

    auto cancelFromSide = [&](auto& sideMap) -> bool {
        auto levelIt = sideMap.find(px);
        if (levelIt == sideMap.end()) {
            // index said it was here but level is gone; clean up
            index_.erase(it);
            return false;
        }

        auto& queue = levelIt->second;
        for (auto qIt = queue.begin(); qIt != queue.end(); ++qIt) {
            if (qIt->id.raw() == id.raw()) {
                // Remove the order from this price level
                queue.erase(qIt);
                if (queue.empty()) {
                    sideMap.erase(levelIt);
                }
                index_.erase(it);
                return true;
            }
        }

        // Didn't find order in this level; clean up index entry anyway
        index_.erase(it);
        return false;
    };

    if (side == Side::Bid) {
        return cancelFromSide(bids_);
    } else {
        return cancelFromSide(asks_);
    }
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    // bids_ is map<Price, LevelQueue, std::greater<Price>>
    // so begin() is the highest bid
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    // asks_ is map<Price, LevelQueue, std::less<Price>>
    // so begin() is the lowest ask
    return asks_.begin()->first;
}

std::vector<LevelInfo> OrderBook::snapshot(Side side) const {
    std::vector<LevelInfo> levels;
    levels.reserve(32); // arbitrary

    auto buildSideSnapshot = [&](auto const& bookSide) {
        for (const auto& [price, queue] : bookSide) {
            std::int64_t sum = 0;
            for (const auto& o : queue) {
                sum += o.remaining.raw();
            }
            levels.push_back(LevelInfo{
                price,
                Quantity{sum}
            });
        }
    };

    if (side == Side::Bid) {
        buildSideSnapshot(bids_);
    } else {
        buildSideSnapshot(asks_);
    }

    return levels;
}

} // namespace map
