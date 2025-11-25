#pragma once

#include <optional>
#include <string>
#include <vector>

#include "map/algo/AlphaEngine.hpp"
#include "map/execution/AlgoExecutor.hpp"
#include "map/Types.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/strategy/StrategyBase.hpp"
#include "map/types/Contract.hpp"

namespace map::strategy {

class GammaScalpStrategy : public StrategyBase {
public:
    struct Params {
        std::string hedgeSymbol{"SPY"};
        int64_t     rebalanceIntervalMs{100};
        double      contractMultiplier{100.0};
        double      innerBand{10.0};   // do nothing within +/- inner
        double      outerBand{50.0};   // hedge once beyond this
        int         straddleQty{1};
        bool        passiveEntry{true}; // enter straddle via pegged orders
    };

    GammaScalpStrategy(EventBus& bus,
                       algo::AlphaEngine& alpha,
                       execution::AlgoExecutor& exec,
                       const Params& p);

    void setStraddle(const types::OptionContract& callC,
                     const types::OptionContract& putC);

    void onOptionUpdate(const OptionGreeksEvent& ev);
    void onUnderlyingUpdate(double price);
    void onTick(std::uint64_t nowMs) override;

private:
    struct Leg {
        std::string symbol;
        bool        isCall{true};
        double      strike{0.0};
        double      delta{0.0};
        double      gamma{0.0};
        double      theta{0.0};
        double      price{0.0};
        double      position{0.0};
    };

    Params params_;
    std::optional<Leg> call_;
    std::optional<Leg> put_;
    double underlyingPrice_{0.0};
    double hedgePosition_{0.0};
    bool   entered_{false};
    std::uint64_t lastRebalanceMs_{0};

    void ensureStraddleEntered();
    void recalcPortfolio();
    void maybeHedge(std::uint64_t nowMs);
    void sendOrder(const std::string& symbol, Side side, int qty, OrderType type, double refPrice, const std::string& clientId);

    algo::AlphaEngine& alpha_;
    execution::AlgoExecutor& exec_;
};

} // namespace map::strategy
