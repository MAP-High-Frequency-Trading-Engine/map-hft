#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

#include "map/Side.hpp"

namespace map {

struct ImpactParams {
    // Temporary impact half-life (in ticks)
    double tempHalfLifeTicks = 500.0;
    // Coefficient for temporary impact (ticks of price per normalized size)
    double tempCoeff         = 2.0;
    // Coefficient for permanent impact (ticks per normalized size)
    double permCoeff         = 0.3;
    // Cap on total impact in ticks (both temp + perm)
    double maxImpactTicks    = 80.0;
    // EWMA parameter for volatility estimation
    double volAlpha          = 0.05;
};

class ImpactModel {
public:
    explicit ImpactModel(const ImpactParams& params = ImpactParams{});

    // Called once per tick per symbol with LOB mid + spread
    void onNewTick(const std::string& symbol, double mid, double spread);

    // Called whenever our strategy trades (simulated)
    // qty: signed notional units (we just treat as size measure)
    // depth: approximate total depth at best levels
    // imbalance: LOB imbalance at this moment
    void onTrade(const std::string& symbol,
                 Side side,
                 double qty,
                 double depth,
                 double imbalance);

    // Mid price the strategy *perceives* after impact
    double effectiveMid(const std::string& symbol, double rawMid) const;

    // Execution price including impact + directional edge
    double executionPrice(const std::string& symbol,
                          Side side,
                          double rawMid,
                          double depth) const;

    // Expose adverse-selection probability for logging / experimentation
    double adverseProbability(const std::string& symbol,
                              double imbalance) const;

private:
    struct SymState {
        bool   hasLastMid   = false;
        double lastMid      = 0.0;
        double lastSpread   = 0.0;

        // EWMA variance of mid changes -> volatility estimate
        double ewmaVar      = 0.0;

        // Impact expressed in ticks (mid in "ticks" units)
        double tempImpact   = 0.0;
        double permImpact   = 0.0;
    };

    ImpactParams params_;
    std::unordered_map<std::string, SymState> state_;

    double clampImpact(double impact) const;
};

} // namespace map
