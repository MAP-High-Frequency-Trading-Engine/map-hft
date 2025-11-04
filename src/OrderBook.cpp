#include "map/OrderBook.hpp"

#include <algorithm>  // std::min

// Helper: generic matching logic that works for either side
template <typename MySideMap, typename OppSideMap>
static void matchIncomingOrder(
    Order& incoming,
    Price limitPrice,
    Side side,
    MySideMap& mySide,
    OppSideMap& oppSide,
    std::map<OrderId, std::pair<Side, Price>>& index
) {
    while (incoming.remaining.raw() > 0 && !oppSide.empty()) {
        auto bestIt = oppSide.begin();
        Price bestPrice = bestIt->first;

        bool crosses = (side == Side::Bid)
                       ? (bestPrice <= limitPrice)  // bid crosses ask
                       : (bestPrice >= limitPrice); // ask crosses bid

        if (!crosses) {
            break;
        }

        auto& queue = bestIt->second;

        while (!queue.empty() && incoming.remaining.raw() > 0) {
            Order& resting = queue.front();

            auto tradedRaw = std::min(
                incoming.remaining.raw(),
                resting.remaining.raw()
            );

            incoming.remaining = Quantity{incoming.remaining.raw() - tradedRaw};
            resting.remaining  = Quantity{resting.remaining.raw()  - tradedRaw};

            // (Later) emit Trade event here

            if (resting.remaining.raw() == 0) {
                index.erase(resting.id);
                queue.pop_front();
            } else {
                // partially filled resting order stays at front
                break;
            }
        }

        if (queue.empty()) {
            oppSide.erase(bestIt);
        }
    }

    // If still quantity left, rest it on my side
    if (incoming.remaining.raw() > 0) {
        auto& levelQueue = mySide[limitPrice];
        levelQueue.push_back(incoming);
        index[incoming.id] = { side, limitPrice };
    }
}

OrderBook::OrderBook() = default;

OrderId OrderBook::addOrder(Side side, Price px, Quantity qty) {
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

    if (side == Side::Bid) {
        auto levelIt = bids_.find(px);
        if (levelIt == bids_.end()) {
            index_.erase(it);
            return false;
        }

        auto& queue = levelIt->second;
        for (auto qIt = queue.begin(); qIt != queue.end(); ++qIt) {
            if (qIt->id.raw() == id.raw()) {
                queue.erase(qIt);
                if (queue.empty()) {
                    bids_.erase(levelIt);
                }
                index_.erase(it);
                return true;
            }
        }

        index_.erase(it);
        return false;
    } else {
        auto levelIt = asks_.find(px);
        if (levelIt == asks_.end()) {
            index_.erase(it);
            return false;
        }

        auto& queue = levelIt->second;
        for (auto qIt = queue.begin(); qIt != queue.end(); ++qIt) {
            if (qIt->id.raw() == id.raw()) {
                queue.erase(qIt);
                if (queue.empty()) {
                    asks_.erase(levelIt);
                }
                index_.erase(it);
                return true;
            }
        }

        index_.erase(it);
        return false;
    }
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}
