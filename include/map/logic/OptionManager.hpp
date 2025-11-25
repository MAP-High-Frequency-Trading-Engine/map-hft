#pragma once

#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "map/alpaca/OptionChainProvider.hpp"
#include "map/analytics/BlackScholes.hpp"
#include "map/core/Event.hpp"
#include "map/types/Contract.hpp"

namespace map::logic {

struct OptionCalcState {
    types::OptionContract contract;
    double                impliedVol{0.5};
    double                lastOptionPrice{0.0};
    double                timeToExpiryYears{1.0 / 365.0};
    analytics::OptionGreeks greeks{};
};

class OptionManager {
public:
    OptionManager(alpaca::OptionChainProvider& provider,
                  double riskFreeRate = 0.0,
                  double defaultIv    = 0.5);

    // Fetch the latest chain from Alpaca's REST API.
    bool refreshChain(const std::string& underlyingSymbol);

    // Select near-ATM contracts based on the already-fetched chain.
    std::vector<std::string> selectATMContracts(double currentUnderlyingPrice, double rangePercent);

    // Manually track a provided symbol list (uses fetched chain to fill metadata).
    std::vector<std::string> trackSpecific(const std::vector<std::string>& symbols,
                                           double currentUnderlyingPrice);

    // Sticky IV calibration on option quote (mid) updates.
    std::optional<OptionGreeksEvent> onOptionQuote(const OptionGreeksEvent& ev, double underlyingPrice);

    // Sticky IV greeks refresh for all tracked contracts on underlying moves.
    std::vector<OptionGreeksEvent> onUnderlyingQuote(double underlyingPrice);

    const std::unordered_map<std::string, OptionCalcState>& tracked() const { return calcState_; }
    const std::vector<types::OptionContract>& chain() const { return chain_; }

    // Pick nearest call/put to current price (for straddle entry).
    std::optional<std::pair<types::OptionContract, types::OptionContract>>
    chooseStraddle(double currentUnderlyingPrice) const;

private:
    alpaca::OptionChainProvider& provider_;
    double riskFreeRate_{0.0};
    double defaultIv_{0.5};

    std::vector<types::OptionContract> chain_;
    std::unordered_map<std::string, OptionCalcState> calcState_;
    std::uint64_t deltaLogCounter_{0};

    static double yearsToExpiry(const types::OptionContract& c);
    static bool parseYMD(const std::string& date, int& y, int& m, int& d);
    static std::time_t expirationUtcTime(const types::OptionContract& c);

    OptionGreeksEvent toEvent(const OptionCalcState& st) const;
};

} // namespace map::logic
