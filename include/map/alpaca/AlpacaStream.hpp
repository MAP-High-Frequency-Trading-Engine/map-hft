#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "map/alpaca/AlpacaClient.hpp"
#include "map/core/Event.hpp"
#include "map/core/LockFreeQueue.hpp"

namespace map::alpaca {

    struct StreamConfig {
        std::vector<std::string> underlyings;
        std::vector<std::string> options;
        bool useSip{false}; // default to free IEX stream unless SIP is requested
    };

    class Stream {
    public:
        Stream(const Credentials& creds,
               LockFreeQueue<MarketDataEvent, 8192>& mdQueue,
               LockFreeQueue<OptionGreeksEvent, 8192>& greeksQueue,
               const StreamConfig& cfg);
        ~Stream();

        void start();
        void stop();

    private:
        void runEquity();
        void runOptions();

        Credentials creds_;
        LockFreeQueue<MarketDataEvent, 8192>& mdQueue_;
        LockFreeQueue<OptionGreeksEvent, 8192>& greeksQueue_;
        StreamConfig cfg_;

        std::atomic<bool> running_{false};
        std::thread equityThread_;
        std::thread optionThread_;
    };

} // namespace map::alpaca
