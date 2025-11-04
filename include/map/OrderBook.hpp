#pragma once

#include <map>
#include <deque>
#include <optional>
#include "order.hpp"

class OrderBook {
public:
    OrderBook();

    OrderId addOrder(Side side, Price px, Quantity qty);
    bool    cancelOrder(OrderId id);

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

private:
    using LevelQueue = std::deque<Order>;

    std::map<Price, LevelQueue, std::greater<Price>> bids_;
    std::map<Price, LevelQueue, std::less<Price>>    asks_;
    std::map<OrderId, std::pair<Side, Price>>        index_;
    std::uint64_t                                    nextId_ = 1;
};
