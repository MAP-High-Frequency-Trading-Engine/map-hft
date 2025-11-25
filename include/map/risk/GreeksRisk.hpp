#pragma once

#include <string>
#include <unordered_map>

#include "map/risk/RiskLimits.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"

namespace map {

    struct GreeksState {
        double totalDelta{0.0};
        double totalGamma{0.0};
        double totalTheta{0.0};
        double maxDrawdown{0.0};
    };

    class GreeksRisk {
    public:
        struct Limits {
            double maxAbsDelta{10'000.0};
            double maxGamma{5'000.0};
            double minTheta{-50'000.0};
            double maxDrawdown{0.20};      // 20%
            double vixKillSwitchPct{5.0};  // 5% spike within window
            double optionMultiplier{100.0};
        };

        GreeksRisk(RiskLimits& baseRisk,
                   EventBus* bus,
                   const Limits& limits);

        // Update latest option greeks snapshot (from feed)
        void updateOptionGreeks(const OptionGreeksEvent& ev);

        // Pre-trade check
        bool check(const NewOrderEvent& order);

        // Apply fill to running exposure state
        void onFill(const NewOrderEvent& order);

        // Monitor VIX spikes for kill switch
        void onVixUpdate(double vix);

        const GreeksState& state() const { return state_; }

    private:
        double orderDelta(const NewOrderEvent& order) const;
        double orderGamma(const NewOrderEvent& order) const;
        double orderTheta(const NewOrderEvent& order) const;
        void   maybeTriggerKillSwitch(double vix);

        RiskLimits& base_;
        EventBus*   bus_;
        Limits      limits_;
        GreeksState state_;

        std::unordered_map<std::string, OptionGreeksEvent> latestOptionGreeks_;
        double lastVix_{0.0};
    };

} // namespace map
