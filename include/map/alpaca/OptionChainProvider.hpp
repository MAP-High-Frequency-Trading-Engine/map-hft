#pragma once

#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "map/alpaca/AlpacaClient.hpp"
#include "map/types/Contract.hpp"

namespace map::alpaca {

class OptionChainProvider {
public:
    OptionChainProvider(const Credentials& creds, std::string feed = "opra");

    std::vector<types::OptionContract> getActiveContracts(const std::string& underlyingSymbol);

    static std::string todayDateET(int offsetDays = 0);

private:
    HttpClient client_;
    std::string dataBaseUrl_; // data host without version suffix
    std::string feed_;

    static std::time_t timegmPortable(std::tm* tm);
    static int easternUtcOffsetHours(const std::tm& utcNow);
    static bool isEasternDST(const std::tm& utcNow);
};

} // namespace map::alpaca
