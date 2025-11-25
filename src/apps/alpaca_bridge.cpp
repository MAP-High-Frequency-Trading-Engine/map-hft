// Connect MAP engine to Alpaca (quotes + live execution)
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "map/alpaca/AlpacaClient.hpp"
#include "map/alpaca/AlpacaExecutor.hpp"
#include "map/alpaca/OptionChainProvider.hpp"
#include "map/alpaca/AlpacaStream.hpp"
#include "map/logic/OptionManager.hpp"
#include "map/risk/GreeksRisk.hpp"
#include "map/strategy/GammaScalpStrategy.hpp"
#include "map/algo/AlphaEngine.hpp"
#include "map/execution/AlgoExecutor.hpp"
#include "map/core/LockFreeQueue.hpp"
#include "map/types/Contract.hpp"
#include <nlohmann/json.hpp>

using namespace map;

std::vector<std::string> splitCSV(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

double fetchUnderlyingMid(alpaca::HttpClient& client, const std::string& symbol, const std::string& feed) {
    std::string path = "/stocks/" + symbol + "/quotes/latest";
    if (feed == "iex" || feed == "sip") {
        path += "?feed=" + feed;
    }
    auto resp = client.getData(path);
    if (resp.status != 200) {
        std::cerr << "[bootstrap] Failed to fetch latest quote for " << symbol
                  << " status=" << resp.status << " body=" << resp.body << std::endl;
        return 0.0;
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (!j.contains("quote")) return 0.0;
        const auto& q = j["quote"];
        double bid = q.value("bp", 0.0);
        double ask = q.value("ap", 0.0);
        double last = q.value("mid", 0.0);
        if (last <= 0.0) {
            last = (bid > 0.0 && ask > 0.0) ? (bid + ask) * 0.5 : 0.0;
        }
        return last;
    } catch (const std::exception& ex) {
        std::cerr << "[bootstrap] Quote parse error: " << ex.what()
                  << " body=" << resp.body << std::endl;
        return 0.0;
    }
}

int main(int argc, char** argv) {
    std::string envFile = ".env.alpaca";
    std::string underlyingList = "SPY";
    std::string optionList;
    std::uint64_t maxTicks = 2000; 
    std::string feed = "iex"; 

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--env=", 0) == 0) {
            envFile = arg.substr(6);
        } else if (arg.rfind("--underlyings=", 0) == 0) {
            underlyingList = arg.substr(std::string("--underlyings=").size());
        } else if (arg.rfind("--options=", 0) == 0) {
            optionList = arg.substr(std::string("--options=").size());
        } else if (arg.rfind("--ticks=", 0) == 0) {
            maxTicks = static_cast<std::uint64_t>(std::stoll(arg.substr(std::string("--ticks=").size())));
        } else if (arg.rfind("--feed=", 0) == 0) {
            feed = arg.substr(std::string("--feed=").size());
        }
    }

    alpaca::Credentials creds;
    if (!alpaca::loadCredentialsFromEnv(envFile, creds)) {
        std::cerr << "Failed to load Alpaca credentials.\n";
        return 1;
    }

    std::cout << "Loaded Alpaca credentials. Feed: " << feed << "\n";

    std::vector<std::string> underlyings = splitCSV(underlyingList);
    if (underlyings.empty()) underlyings.push_back("SPY");
    std::vector<std::string> options = splitCSV(optionList);

    LockFreeQueue<MarketDataEvent, 8192> mdQueue;
    LockFreeQueue<OptionGreeksEvent, 8192> greeksQueue;

    EventBus bus;
    RiskLimits baseRisk;
    GreeksRisk::Limits glimits;
    GreeksRisk greeksRisk(baseRisk, &bus, glimits);

    algo::AlphaConfig alphaCfg;
    alphaCfg.obiThreshold = 0.15;
    algo::AlphaEngine alpha(alphaCfg);

    execution::AlgoParams algoParams;
    algoParams.chaseIntervalMs = 150;
    algoParams.maxChaseMs = 800;
    algoParams.minPriceMove = 0.005;
    execution::AlgoExecutor hedgeAlgo(bus, creds, algoParams);

    strategy::GammaScalpStrategy::Params stratParams;
    stratParams.hedgeSymbol = underlyings.front();
    stratParams.rebalanceIntervalMs = 250;
    stratParams.innerBand = 15.0;
    stratParams.outerBand = 60.0;
    stratParams.straddleQty = 1;
    strategy::GammaScalpStrategy strategy(bus, alpha, hedgeAlgo, stratParams);

    double riskFreeRate = 0.0525; 
    // Option chain feed can differ from equity feed; fall back to indicative when requested.
    std::string optionFeed = (feed == "indicative") ? "indicative" : "opra";
    alpaca::OptionChainProvider chainProvider(creds, optionFeed);
    logic::OptionManager optionManager(chainProvider, riskFreeRate, 0.50);
    alpaca::HttpClient dataClient(creds);

    double lastUnderlyingMid = fetchUnderlyingMid(dataClient, stratParams.hedgeSymbol, feed);
    if (lastUnderlyingMid > 0.0) {
        std::cout << "[bootstrap] " << stratParams.hedgeSymbol
                  << " mid=" << lastUnderlyingMid << std::endl;
    } else {
        std::cout << "[bootstrap] Could not bootstrap price for "
                  << stratParams.hedgeSymbol << " (will wait for stream)." << std::endl;
    }

    std::optional<std::pair<types::OptionContract, types::OptionContract>> straddle;

    if (options.empty()) {
        if (optionManager.refreshChain(underlyings.front())) {
            if (lastUnderlyingMid > 0.0) {
                straddle = optionManager.chooseStraddle(lastUnderlyingMid);
                if (straddle) {
                    options = optionManager.trackSpecific({straddle->first.symbol, straddle->second.symbol}, lastUnderlyingMid);
                    strategy.setStraddle(straddle->first, straddle->second);
                }
            } else {
                std::cout << "[bootstrap] Skipping option selection until price is known." << std::endl;
            }
        } else {
            std::cout << "[bootstrap] Failed to load option chain for " << underlyings.front() << std::endl;
        }
    } else {
        optionManager.refreshChain(underlyings.front());
        if (lastUnderlyingMid > 0.0) {
            auto tracked = optionManager.trackSpecific(options, lastUnderlyingMid);
            if (!tracked.empty()) options = tracked;
        }
    }

    if (options.empty()) {
        std::cout << "[bootstrap] No option symbols selected; option stream will remain idle." << std::endl;
    } else {
        std::cout << "[bootstrap] Subscribing to options:";
        for (const auto& s : options) std::cout << " " << s;
        std::cout << std::endl;
    }


    std::atomic<bool> running{true};

    bus.subscribe<OptionGreeksEvent>([&](const OptionGreeksEvent& ev) {
        auto recalibrated = optionManager.onOptionQuote(ev, lastUnderlyingMid);
        if (recalibrated) {
            greeksRisk.updateOptionGreeks(*recalibrated);
            strategy.onOptionUpdate(*recalibrated);
        } else {
            greeksRisk.updateOptionGreeks(ev);
            strategy.onOptionUpdate(ev);
        }
    });

    bus.subscribe<MarketDataEvent>([&](const MarketDataEvent& ev) {
        alpha.onMarketData(ev);
        hedgeAlgo.onMarketData(ev);
        std::cout << "[QUOTE] " << ev.symbol 
                  << " Bid:" << ev.bid << "x" << ev.bidSize 
                  << " Ask:" << ev.ask << "x" << ev.askSize 
                  << " Last:" << ev.last
                  << std::endl;
        if (ev.timestamp > 0.0) {
            uint64_t evtNs = static_cast<uint64_t>(ev.timestamp);
            uint64_t nowNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            if (nowNs > evtNs) {
                uint64_t latencyNs = nowNs - evtNs;
                if (latencyNs > 500'000'000ULL) {
                    std::cout << "[WARN] Market data latency " << (latencyNs / 1'000'000.0)
                              << " ms. Pausing trading." << std::endl;
                    running = false;
                    return;
                }
            }
        }

        double px = ev.last > 0.0 ? ev.last : (ev.bid + ev.ask) * 0.5;
        if (!std::isnan(px) && px > 0.0) {
            strategy.onUnderlyingUpdate(px);
            
            if (ev.symbol == stratParams.hedgeSymbol) {
                lastUnderlyingMid = px; 
                auto stickyGreeks = optionManager.onUnderlyingQuote(px);
                for (const auto& optEv : stickyGreeks) {
                    greeksRisk.updateOptionGreeks(optEv);
                    strategy.onOptionUpdate(optEv);
                }
            }
        }
        if (ev.symbol == "VIX") {
            greeksRisk.onVixUpdate(px);
        }
    });

    bus.subscribe<KillSwitchEvent>([&](const KillSwitchEvent& ev) {
        std::cout << "KillSwitch triggered: " << ev.reason << std::endl;
        running = false;
    });

    bus.subscribe<OrderAckEvent>([&](const OrderAckEvent& ack) {
        hedgeAlgo.onOrderAck(ack);
        std::cout << "OrderAck: " << ack.clientOrderId << " Accepted: " << ack.accepted << std::endl;
    });

    alpaca::StreamConfig scfg{underlyings, options};
    scfg.useSip = (feed == "sip");
    alpaca::Stream stream(creds, mdQueue, greeksQueue, scfg);
    
    alpaca::Executor exec(bus, &greeksRisk, creds);

    stream.start();
    std::cout << "Waiting for data stream..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Engine Loop Started." << std::endl;

    for (std::uint64_t t = 0; t < maxTicks && running; ++t) {
        bool busy = false;

        MarketDataEvent md{};
        while (mdQueue.pop(md)) {
            bus.publish(md);
            busy = true;
        }

        OptionGreeksEvent ge{};
        while (greeksQueue.pop(ge)) {
            bus.publish(ge);
            busy = true;
        }

        strategy.onTick(t * stratParams.rebalanceIntervalMs);
        hedgeAlgo.onTick(t * stratParams.rebalanceIntervalMs);

        if (!busy) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    stream.stop();
    std::cout << "alpaca_bridge shutdown\n";
    return 0;
}
