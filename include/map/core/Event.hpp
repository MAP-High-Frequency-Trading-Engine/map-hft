#pragma once
#include <string>
#include "map/Types.hpp"
#include "map/Side.hpp"

namespace map {

    // Base event type (for logging / tagging)
    struct EventBase {
        // could add timestamp, seq num later
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
        Side        takerSide; // Bid or Ask
        Price       price;
        Quantity    qty;
    };

} // namespace map
