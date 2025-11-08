#pragma once

#include <string>
#include "map/Types.hpp"
#include "map/Side.hpp"

namespace map {

    // Base type so EventBus can type-erase everything
    struct EventBase {
        virtual ~EventBase() = default;
    };

    struct NewOrderEvent : EventBase {
        std::string symbol;
        Side        side;
        Price       price;
        Quantity    qty;
    };

    struct CancelOrderEvent : EventBase {
        std::string symbol;
        OrderId     id;
    };

    struct TradeEvent : EventBase {
        std::string symbol;
        OrderId     takerId;
        OrderId     makerId;
        Side        takerSide;
        Price       price;
        Quantity    qty;
    };

} // namespace map
