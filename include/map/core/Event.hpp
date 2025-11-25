#pragma once

#include <string>
#include "map/Types.hpp"
#include "map/Side.hpp"
#include <optional>

namespace map {

    // Base type so EventBus can type-erase everything
    struct EventBase {
        virtual ~EventBase() = default;
    };

    enum class OrderType {
        Market,
        Limit
    };

    enum class TimeInForce {
        Day,
        IOC,
        FOK,
        GTC
    };

    struct NewOrderEvent : EventBase {
        std::string symbol;
        Side        side;
        Price       price;
        Quantity    qty;
        OrderType   type{OrderType::Limit};
        TimeInForce tif{TimeInForce::Day};
        std::string clientOrderId;
        bool        extendedHours{false};
        bool        allowPartial{true};
        std::optional<double> notional; // optional notional-based order for market ETFs
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

    struct MarketDataEvent : EventBase {
        std::string symbol;
        double      bid{0.0};
        double      ask{0.0};
        double      last{0.0};
        double      bidSize{0.0};
        double      askSize{0.0};
        double      timestamp{0.0};
    };

    struct OptionGreeksEvent : EventBase {
        std::string symbol;
        double      price{0.0};
        double      delta{0.0};
        double      gamma{0.0};
        double      theta{0.0};
        double      vega{0.0};
        std::optional<double> position; // optional signed contracts currently held
    };

    struct OrderAckEvent : EventBase {
        std::string symbol;
        std::string clientOrderId;
        std::string brokerOrderId;
        bool        accepted{true};
        std::string message;
    };

    struct CancelAllEvent : EventBase {
        std::string reason;
    };

    struct KillSwitchEvent : EventBase {
        std::string reason;
    };

} // namespace map
