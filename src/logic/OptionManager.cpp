#include "map/logic/OptionManager.hpp"

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace map::logic {
namespace {

int dayOfWeek(int y, int m, int d) {
    // Sakamoto algorithm, returns 0 = Sunday
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

int nthWeekdayOfMonth(int year, int month, int weekday, int nth) {
    int firstDow = dayOfWeek(year, month, 1);
    int day = 1 + (7 + weekday - firstDow) % 7;
    day += 7 * (nth - 1);
    return day;
}

bool isEasternDSTDate(int year, int month, int day) {
    int dstStartDay = nthWeekdayOfMonth(year, 3, 0, 2);  // second Sunday in March
    int dstEndDay   = nthWeekdayOfMonth(year, 11, 0, 1); // first Sunday in November

    if (month < 3 || month > 11) return false;
    if (month > 3 && month < 11) return true;
    if (month == 3) return day >= dstStartDay;
    return day < dstEndDay;
}

std::time_t timegmPortable(std::tm* tm) {
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

} // namespace

OptionManager::OptionManager(alpaca::OptionChainProvider& provider,
                             double riskFreeRate,
                             double defaultIv)
    : provider_(provider),
      riskFreeRate_(riskFreeRate),
      defaultIv_(defaultIv) {}

bool OptionManager::refreshChain(const std::string& underlyingSymbol) {
    chain_ = provider_.getActiveContracts(underlyingSymbol);
    return !chain_.empty();
}

std::vector<std::string> OptionManager::selectATMContracts(double currentUnderlyingPrice,
                                                           double rangePercent) {
    std::vector<std::string> syms;
    if (currentUnderlyingPrice <= 0.0) return syms;

    calcState_.clear();
    for (const auto& c : chain_) {
        double rel = std::abs(c.strike - currentUnderlyingPrice) / currentUnderlyingPrice;
        if (rel < rangePercent) {
            OptionCalcState st;
            st.contract = c;
            st.impliedVol = defaultIv_;
            st.timeToExpiryYears = yearsToExpiry(c);
            st.greeks = analytics::calculateBS(currentUnderlyingPrice,
                                               c.strike,
                                               st.timeToExpiryYears,
                                               riskFreeRate_,
                                               st.impliedVol,
                                               c.isCall);
            calcState_[c.symbol] = st;
            syms.push_back(c.symbol);
        }
    }
    return syms;
}

std::vector<std::string> OptionManager::trackSpecific(const std::vector<std::string>& symbols,
                                                      double currentUnderlyingPrice) {
    std::vector<std::string> syms;
    if (symbols.empty() || currentUnderlyingPrice <= 0.0) return syms;

    std::unordered_set<std::string> want(symbols.begin(), symbols.end());
    calcState_.clear();
    for (const auto& c : chain_) {
        if (want.find(c.symbol) == want.end()) continue;

        OptionCalcState st;
        st.contract = c;
        st.impliedVol = defaultIv_;
        st.timeToExpiryYears = yearsToExpiry(c);
        st.greeks = analytics::calculateBS(currentUnderlyingPrice,
                                           c.strike,
                                           st.timeToExpiryYears,
                                           riskFreeRate_,
                                           st.impliedVol,
                                           c.isCall);
        calcState_[c.symbol] = st;
        syms.push_back(c.symbol);
    }
    return syms;
}

std::optional<OptionGreeksEvent> OptionManager::onOptionQuote(const OptionGreeksEvent& ev,
                                                              double underlyingPrice) {
    if (underlyingPrice <= 0.0) return std::nullopt;
    auto it = calcState_.find(ev.symbol);
    if (it == calcState_.end()) return std::nullopt;

    OptionCalcState& st = it->second;
    st.timeToExpiryYears = yearsToExpiry(st.contract);

    double newIv = analytics::impliedVolNewton(
        ev.price,
        underlyingPrice,
        st.contract.strike,
        st.timeToExpiryYears,
        riskFreeRate_,
        st.contract.isCall,
        st.impliedVol);

    if (newIv > 0.001 && newIv < 5.0) {
        st.impliedVol = newIv;
    }

    st.greeks = analytics::calculateBS(underlyingPrice,
                                       st.contract.strike,
                                       st.timeToExpiryYears,
                                       riskFreeRate_,
                                       st.impliedVol,
                                       st.contract.isCall);
    st.greeks.price = ev.price; // retain observed mid as price
    st.lastOptionPrice = ev.price;

    return toEvent(st);
}

std::vector<OptionGreeksEvent> OptionManager::onUnderlyingQuote(double underlyingPrice) {
    std::vector<OptionGreeksEvent> out;
    out.reserve(calcState_.size());
    if (underlyingPrice <= 0.0) return out;

    double totalDelta = 0.0;
    for (auto& kv : calcState_) {
        auto& st = kv.second;
        st.timeToExpiryYears = yearsToExpiry(st.contract);
        st.greeks = analytics::calculateBS(underlyingPrice,
                                           st.contract.strike,
                                           st.timeToExpiryYears,
                                           riskFreeRate_,
                                           st.impliedVol,
                                           st.contract.isCall);
        // No fresh quote; use theoretical price from sticky IV
        st.lastOptionPrice = st.greeks.price;
        totalDelta += st.greeks.delta * 100.0; // contract multiplier
        out.push_back(toEvent(st));
    }

    if (!calcState_.empty() && (++deltaLogCounter_ % 20 == 0)) {
        std::cout << "[OptionManager] Sticky total delta (100x contracts): "
                  << totalDelta << std::endl;
    }

    return out;
}

bool OptionManager::parseYMD(const std::string& date, int& y, int& m, int& d) {
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
    return y > 1970 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

std::time_t OptionManager::expirationUtcTime(const types::OptionContract& c) {
    int y = 0, m = 0, d = 0;
    if (!parseYMD(c.expiration, y, m, d)) {
        return 0;
    }

    bool isDst = isEasternDSTDate(y, m, d);
    int offset = isDst ? -4 : -5;

    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = 16 - offset; // convert 16:00 ET into UTC
    tm.tm_min  = 0;
    tm.tm_sec  = 0;

    return timegmPortable(&tm);
}

double OptionManager::yearsToExpiry(const types::OptionContract& c) {
    std::time_t expiryUtc = expirationUtcTime(c);
    if (expiryUtc <= 0) return 1.0 / 365.0;

    auto now = std::chrono::system_clock::now();
    std::time_t nowUtc = std::chrono::system_clock::to_time_t(now);
    double secondsLeft = std::difftime(expiryUtc, nowUtc);
    if (secondsLeft < 60.0) secondsLeft = 60.0;

    return secondsLeft / (365.0 * 24.0 * 3600.0);
}

OptionGreeksEvent OptionManager::toEvent(const OptionCalcState& st) const {
    OptionGreeksEvent ev{};
    ev.symbol = st.contract.symbol;
    ev.price  = st.lastOptionPrice;
    ev.delta  = st.greeks.delta;
    ev.gamma  = st.greeks.gamma;
    ev.theta  = st.greeks.theta;
    ev.vega   = st.greeks.vega;
    return ev;
}

std::optional<std::pair<types::OptionContract, types::OptionContract>>
OptionManager::chooseStraddle(double currentUnderlyingPrice) const {
    if (currentUnderlyingPrice <= 0.0 || chain_.empty()) return std::nullopt;

    const types::OptionContract* bestCall = nullptr;
    const types::OptionContract* bestPut  = nullptr;
    double bestCallDiff = std::numeric_limits<double>::max();
    double bestPutDiff  = std::numeric_limits<double>::max();

    for (const auto& c : chain_) {
        double diff = std::abs(c.strike - currentUnderlyingPrice);
        if (c.isCall) {
            if (diff < bestCallDiff) {
                bestCallDiff = diff;
                bestCall = &c;
            }
        } else {
            if (diff < bestPutDiff) {
                bestPutDiff = diff;
                bestPut = &c;
            }
        }
    }

    if (!bestCall || !bestPut) return std::nullopt;
    return std::make_pair(*bestCall, *bestPut);
}

} // namespace map::logic
