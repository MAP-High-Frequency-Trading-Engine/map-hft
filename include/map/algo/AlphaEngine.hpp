#pragma once

#include <optional>

#include "map/core/Event.hpp"

namespace map::algo {

struct AlphaConfig {
    double obiThreshold{0.2};
};

class AlphaEngine {
public:
    explicit AlphaEngine(const AlphaConfig& cfg) : cfg_(cfg) {}

    void onMarketData(const MarketDataEvent& md) { lastMd_ = md; }

    std::optional<double> obi() const {
        if (!lastMd_) return std::nullopt;
        double bs = lastMd_->bidSize;
        double as = lastMd_->askSize;
        double denom = bs + as;
        if (denom <= 0.0) return 0.0;
        return (bs - as) / denom;
    }

    bool allowAggressiveBuy() const {
        auto v = obi();
        return v && *v > cfg_.obiThreshold;
    }

    bool allowAggressiveSell() const {
        auto v = obi();
        return v && *v < -cfg_.obiThreshold;
    }

private:
    AlphaConfig cfg_;
    std::optional<MarketDataEvent> lastMd_;
};

} // namespace map::algo
