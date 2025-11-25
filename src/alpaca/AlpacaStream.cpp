#include "map/alpaca/AlpacaStream.hpp"
#include <chrono>
#include <ctime>
#include <cstdio>
#include <iostream>
#include <thread>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <asio/ssl/context.hpp>
#include <nlohmann/json.hpp>

namespace map::alpaca {

using WsClient = websocketpp::client<websocketpp::config::asio_tls_client>;

namespace {
    double parseNumber(const nlohmann::json& j, const char* key) {
        if (!j.contains(key)) return 0.0;
        const auto& v = j.at(key);
        if (v.is_number()) return v.get<double>();
        if (v.is_string()) {
            try { return std::stod(v.get<std::string>()); } catch (...) { return 0.0; }
        }
        return 0.0;
    }

    uint64_t parseTimestamp(const nlohmann::json& j, const char* key) {
        if (!j.contains(key)) return 0;
        const auto& v = j.at(key);
        if (v.is_number_integer()) return v.get<uint64_t>();
        if (v.is_string()) {
            std::string s = v.get<std::string>();
            int Y, M, D, h, m, sec;
            long nano = 0;
            if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%ldZ",
                            &Y, &M, &D, &h, &m, &sec, &nano) >= 6) {
                std::tm tm{};
                tm.tm_year = Y - 1900;
                tm.tm_mon  = M - 1;
                tm.tm_mday = D;
                tm.tm_hour = h;
                tm.tm_min  = m;
                tm.tm_sec  = sec;
                std::time_t t = timegm(&tm);
                if (t > 0) {
                    return static_cast<uint64_t>(t) * 1'000'000'000ULL + static_cast<uint64_t>(nano);
                }
            }
        }
        return 0; 
    }

    std::string makeAuth(const Credentials& c) {
        nlohmann::json j;
        j["action"] = "auth";
        j["key"] = c.keyId;
        j["secret"] = c.secretKey;
        return j.dump();
    }

    std::string makeSub(const std::vector<std::string>& quotes, bool /*isOption*/) {
        nlohmann::json j;
        j["action"] = "subscribe";
        if (!quotes.empty()) j["quotes"] = quotes; 
        return j.dump();
    }

auto makeTlsContext() {
        return std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
    }
}

Stream::Stream(const Credentials& creds,
               LockFreeQueue<MarketDataEvent, 8192>& mdQueue,
               LockFreeQueue<OptionGreeksEvent, 8192>& greeksQueue,
               const StreamConfig& cfg)
    : creds_(creds),
      mdQueue_(mdQueue),
      greeksQueue_(greeksQueue),
      cfg_(cfg)
{
}

Stream::~Stream() {
    stop();
}

void Stream::start() {
    if (running_) return;
    running_ = true;
    equityThread_ = std::thread([this]() { runEquity(); });
    optionThread_ = std::thread([this]() { runOptions(); });
}

void Stream::stop() {
    running_ = false;
    if (equityThread_.joinable()) equityThread_.join();
    if (optionThread_.joinable()) optionThread_.join();
}

void Stream::runEquity() {
    if (cfg_.underlyings.empty()) return;
    
    auto c = std::make_shared<WsClient>();
    
    c->set_access_channels(websocketpp::log::alevel::none); 
    c->set_error_channels(websocketpp::log::elevel::all);
    c->init_asio();
    
    c->set_tls_init_handler([](websocketpp::connection_hdl) { return makeTlsContext(); });

    c->set_open_handler([this, c](websocketpp::connection_hdl hdl) {
        std::cout << "[AlpacaStream] Equity Connected. Sending Auth..." << std::endl;
        websocketpp::lib::error_code ec;
        c->send(hdl, makeAuth(creds_), websocketpp::frame::opcode::text, ec);
    });

    c->set_message_handler([this, c](websocketpp::connection_hdl hdl, WsClient::message_ptr msg) {
        try {
            auto payload = msg->get_payload();
            auto arr = nlohmann::json::parse(payload);
            
            for (auto& item : arr) {
                std::string type = item.value("T", "");
                
                if (type == "success" && item.value("msg", "") == "authenticated") {
                    std::cout << "[AlpacaStream] Equity Authenticated. Subscribing..." << std::endl;
                    websocketpp::lib::error_code ec;
                    c->send(hdl, makeSub(cfg_.underlyings, false), websocketpp::frame::opcode::text, ec);
                }
                else if (type == "subscription") {
                    std::cout << "[AlpacaStream] Equity Subscription Confirmed: " << payload << std::endl;
                }
                else if (type == "q") {
                    MarketDataEvent ev{};
                    ev.symbol = item.value("S", "");
                    ev.bid    = parseNumber(item, "bp");
                    ev.ask    = parseNumber(item, "ap");
                    ev.bidSize = parseNumber(item, "bs"); 
                    ev.askSize = parseNumber(item, "as");
                    
                    if (ev.bid > 0 && ev.ask > 0) {
                        ev.last = (ev.bid + ev.ask) / 2.0;
                        mdQueue_.push(ev);
                    }
                }
                else if (type == "error") {
                    std::cerr << "[AlpacaStream] Equity Error: " << payload << std::endl;
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[AlpacaStream] JSON Error: " << ex.what() << " Payload: " << msg->get_payload() << std::endl;
        }
    });

    websocketpp::lib::error_code ec;
    std::string endpoint = cfg_.useSip
                               ? "wss://stream.data.alpaca.markets/v2/sip"
                               : "wss://stream.data.alpaca.markets/v2/iex";
                               
    auto con = c->get_connection(endpoint, ec);
    if (ec) {
        std::cerr << "[AlpacaStream] Connect Error: " << ec.message() << std::endl;
        return;
    }
    c->connect(con);

    std::cout << "[AlpacaStream] Equity Thread Started." << std::endl;
    c->run(); 
    std::cout << "[AlpacaStream] Equity Thread Stopped." << std::endl;
}

void Stream::runOptions() {
    if (cfg_.options.empty()) return;
    
    auto c = std::make_shared<WsClient>();
    c->set_access_channels(websocketpp::log::alevel::none);
    c->init_asio();
    c->set_tls_init_handler([](websocketpp::connection_hdl) { return makeTlsContext(); });

    c->set_open_handler([this, c](websocketpp::connection_hdl hdl) {
        std::cout << "[AlpacaStream] Option Connected. Sending Auth (msgpack)..." << std::endl;
        nlohmann::json auth;
        auth["action"] = "auth";
        auth["key"] = creds_.keyId;
        auth["secret"] = creds_.secretKey;
        std::vector<uint8_t> bin = nlohmann::json::to_msgpack(auth);
        websocketpp::lib::error_code ec;
        c->send(hdl, bin.data(), bin.size(), websocketpp::frame::opcode::binary, ec);
    });

    c->set_message_handler([this, c](websocketpp::connection_hdl hdl, WsClient::message_ptr msg) {
        try {
            auto payload = msg->get_payload();
            auto arr = nlohmann::json::from_msgpack(payload.begin(), payload.end());

            for (auto& item : arr) {
                std::string type = item.value("T", "");

                if (type == "success" && item.value("msg", "") == "authenticated") {
                    nlohmann::json sub;
                    sub["action"] = "subscribe";
                    sub["quotes"] = cfg_.options;
                    std::vector<uint8_t> subBin = nlohmann::json::to_msgpack(sub);
                    websocketpp::lib::error_code ec;
                    c->send(hdl, subBin.data(), subBin.size(), websocketpp::frame::opcode::binary, ec);
                    std::cout << "[AlpacaStream] Option subscription sent." << std::endl;
                } else if (type == "q") {
                    OptionGreeksEvent ev{};
                    ev.symbol = item.value("S", "");
                    double bid = parseNumber(item, "bp");
                    double ask = parseNumber(item, "ap");
                    ev.price = (bid > 0 && ask > 0) ? (bid + ask) * 0.5 : 0.0;
                    ev.delta = parseNumber(item, "delta");
                    ev.gamma = parseNumber(item, "gamma");
                    ev.theta = parseNumber(item, "theta");
                    greeksQueue_.push(ev);
                } else if (type == "error") {
                    std::cerr << "[AlpacaStream] Option error: " << item.dump() << std::endl;
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[AlpacaStream] Option parse error: " << ex.what() << std::endl;
        }
    });

    websocketpp::lib::error_code ec;
    auto con = c->get_connection("wss://stream.data.alpaca.markets/v1beta1/opra", ec);
    if (ec) return;
    con->append_header("Content-Type", "application/msgpack");
    c->connect(con);
    c->run(); 
}

} // namespace map::alpaca
