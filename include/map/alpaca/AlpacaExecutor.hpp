#pragma once

#include <string>

#include "map/alpaca/AlpacaClient.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/risk/GreeksRisk.hpp"
#include "map/core/LockFreeQueue.hpp"
#include <atomic>
#include <thread>

namespace map::alpaca {

    class Executor {
    public:
        Executor(EventBus& bus, GreeksRisk* risk, const Credentials& creds);
        ~Executor();

        void onNewOrder(const NewOrderEvent& e);
        void onCancelAll(const CancelAllEvent& e);

    private:
        HttpClient client_;
        EventBus&  bus_;
        GreeksRisk* risk_;
        LockFreeQueue<NewOrderEvent, 1024> pending_;
        std::thread worker_;
        std::atomic<bool> running_{true};
        void runLoop();

        static std::string sideToString(Side s);
        static std::string typeToString(OrderType t);
        static std::string tifToString(TimeInForce t);
    };

} // namespace map::alpaca
