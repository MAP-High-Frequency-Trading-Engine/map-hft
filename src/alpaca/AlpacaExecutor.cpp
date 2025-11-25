#include "map/alpaca/AlpacaExecutor.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace map::alpaca {

Executor::Executor(EventBus& bus, GreeksRisk* risk, const Credentials& creds)
    : client_(creds),
      bus_(bus),
      risk_(risk)
{
    bus_.subscribe<NewOrderEvent>([this](const NewOrderEvent& e) {
        onNewOrder(e);
    });

    bus_.subscribe<CancelAllEvent>([this](const CancelAllEvent& e) {
        onCancelAll(e);
    });

    worker_ = std::thread([this]() { runLoop(); });
}

Executor::~Executor() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string Executor::sideToString(Side s) {
    return (s == Side::Bid) ? "buy" : "sell";
}

std::string Executor::typeToString(OrderType t) {
    return (t == OrderType::Market) ? "market" : "limit";
}

std::string Executor::tifToString(TimeInForce t) {
    switch (t) {
        case TimeInForce::IOC: return "ioc";
        case TimeInForce::FOK: return "fok";
        case TimeInForce::GTC: return "gtc";
        case TimeInForce::Day:
        default: return "day";
    }
}

void Executor::onNewOrder(const NewOrderEvent& e) {
    if (risk_ && !risk_->check(e)) {
        OrderAckEvent ack{};
        ack.symbol = e.symbol;
        ack.clientOrderId = e.clientOrderId;
        ack.accepted = false;
        ack.message = "Risk rejected order";
        bus_.publish(ack);
        return;
    }

    std::string clientId = e.clientOrderId;
    if (clientId.empty()) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        clientId = "map-" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    if (!pending_.push(e)) {
        OrderAckEvent ack{};
        ack.symbol = e.symbol;
        ack.clientOrderId = clientId;
        ack.accepted = false;
        ack.message = "Executor queue full";
        bus_.publish(ack);
        return;
    }
}

void Executor::runLoop() {
    while (running_) {
        NewOrderEvent e{};
        if (!pending_.pop(e)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        std::string clientId = e.clientOrderId;
        if (clientId.empty()) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            clientId = "map-" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        }

        std::ostringstream body;
        body << "{";
        body << "\"symbol\":\"" << e.symbol << "\",";
        body << "\"side\":\"" << sideToString(e.side) << "\",";
        body << "\"type\":\"" << typeToString(e.type) << "\",";
        body << "\"time_in_force\":\"" << tifToString(e.tif) << "\",";
        body << "\"client_order_id\":\"" << clientId << "\",";
        if (e.notional.has_value()) {
            body << "\"notional\":" << *e.notional << ",";
        } else {
            body << "\"qty\":" << e.qty.raw() << ",";
        }
        if (e.type == OrderType::Limit) {
            body << "\"limit_price\":" << e.price.raw() << ",";
        }
        body << "\"extended_hours\":" << (e.extendedHours ? "true" : "false");
        body << "}";

        auto resp = client_.postJson("/orders", body.str());

        OrderAckEvent ack{};
        ack.symbol = e.symbol;
        ack.clientOrderId = clientId;
        ack.brokerOrderId = extractString(resp.body, "id");
        ack.accepted = (resp.status >= 200 && resp.status < 300);
        ack.message = resp.body;

        if (ack.accepted && risk_) {
            risk_->onFill(e); // optimistic fill
        }

        std::cout << "[EXECUTOR] Alpaca order "
                  << (ack.accepted ? "accepted" : "rejected")
                  << " status=" << resp.status << " body=" << resp.body
                  << std::endl;

        bus_.publish(ack);
    }
}

void Executor::onCancelAll(const CancelAllEvent& e) {
    auto resp = client_.deletePath("/orders");
    std::cout << "[EXECUTOR] CancelAll reason=" << e.reason
              << " status=" << resp.status << std::endl;
}

} // namespace map::alpaca
