#include "map/strategy/GammaScalpStrategy.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace map::strategy;
using map::NewOrderEvent;
using map::OrderType;
using map::Price;
using map::Quantity;
using map::Side;

GammaScalpStrategy::GammaScalpStrategy(EventBus& bus,
                                       algo::AlphaEngine& alpha,
                                       execution::AlgoExecutor& exec,
                                       const Params& p)
    : StrategyBase(bus),
      params_(p),
      alpha_(alpha),
      exec_(exec) {}

void GammaScalpStrategy::setStraddle(const types::OptionContract& callC,
                                     const types::OptionContract& putC) {
    call_ = Leg{callC.symbol, true, callC.strike};
    put_  = Leg{putC.symbol, false, putC.strike};
}

void GammaScalpStrategy::onOptionUpdate(const OptionGreeksEvent& ev) {
    if (call_ && ev.symbol == call_->symbol) {
        call_->delta = ev.delta;
        call_->gamma = ev.gamma;
        call_->theta = ev.theta;
        call_->price = ev.price;
    } else if (put_ && ev.symbol == put_->symbol) {
        put_->delta = ev.delta;
        put_->gamma = ev.gamma;
        put_->theta = ev.theta;
        put_->price = ev.price;
    }
}

void GammaScalpStrategy::onUnderlyingUpdate(double price) {
    underlyingPrice_ = price;
}

void GammaScalpStrategy::ensureStraddleEntered() {
    if (entered_ || !call_ || !put_) return;
    if (underlyingPrice_ <= 0.0) return;

    int qty = std::max(1, params_.straddleQty);
    if (params_.passiveEntry) {
        if (!exec_.hasActiveIntent(call_->symbol, Side::Bid)) {
            execution::OrderIntent intent;
            intent.intentId = "enter-call";
            intent.symbol = call_->symbol;
            intent.side = Side::Bid;
            intent.qty = qty;
            intent.allowCross = false;
            exec_.submitIntent(intent);
        }
        if (!exec_.hasActiveIntent(put_->symbol, Side::Bid)) {
            execution::OrderIntent intent;
            intent.intentId = "enter-put";
            intent.symbol = put_->symbol;
            intent.side = Side::Bid;
            intent.qty = qty;
            intent.allowCross = false;
            exec_.submitIntent(intent);
        }
    } else {
        sendOrder(call_->symbol, Side::Bid, qty, OrderType::Market, call_->price, "gamma-straddle-call");
        sendOrder(put_->symbol, Side::Bid, qty, OrderType::Market, put_->price, "gamma-straddle-put");
    }

    call_->position += qty;
    put_->position  += qty;
    entered_ = true;

    std::cout << "[GammaScalp] Entered straddle " << call_->symbol << " & " << put_->symbol
              << " qty=" << qty << std::endl;
}

void GammaScalpStrategy::recalcPortfolio() {
    // Placeholder for extended risk metrics if needed later.
}

void GammaScalpStrategy::maybeHedge(std::uint64_t nowMs) {
    if (underlyingPrice_ <= 0.0) return;
    if (nowMs < lastRebalanceMs_ + static_cast<std::uint64_t>(params_.rebalanceIntervalMs)) return;

    double deltaOpts = 0.0;
    if (call_) deltaOpts += call_->delta * call_->position * params_.contractMultiplier;
    if (put_)  deltaOpts += put_->delta  * put_->position  * params_.contractMultiplier;

    double netDelta = deltaOpts - hedgePosition_;
    double inner = params_.innerBand;
    double outer = params_.outerBand;
    if (std::abs(netDelta) < inner) return;
    if (std::abs(netDelta) < outer) {
        return; // hold within gray band
    }

    Side side = (netDelta > 0) ? Side::Ask : Side::Bid; // sell if long delta
    int qty = static_cast<int>(std::ceil(std::abs(netDelta)));
    if (qty <= 0) qty = 1;

    if (exec_.hasActiveIntent(params_.hedgeSymbol, side)) return;

    bool allowCross = (side == Side::Bid) ? alpha_.allowAggressiveBuy()
                                          : alpha_.allowAggressiveSell();

    execution::OrderIntent intent;
    intent.intentId = (side == Side::Bid ? "hedge-buy" : "hedge-sell");
    intent.symbol = params_.hedgeSymbol;
    intent.side = side;
    intent.qty = qty;
    intent.allowCross = allowCross;
    exec_.submitIntent(intent);

    lastRebalanceMs_ = nowMs;
}

void GammaScalpStrategy::sendOrder(const std::string& symbol,
                                   Side side,
                                   int qty,
                                   OrderType type,
                                   double refPrice,
                                   const std::string& clientId) {
    NewOrderEvent ord{};
    ord.symbol = symbol;
    ord.side   = side;
    ord.qty    = Quantity{qty};
    ord.type   = type;
    ord.price  = Price{refPrice};
    ord.clientOrderId = clientId;
    ord.tif    = TimeInForce::Day;
    bus_.publish(ord);
}

void GammaScalpStrategy::onTick(std::uint64_t nowMs) {
    ensureStraddleEntered();
    recalcPortfolio();
    maybeHedge(nowMs);
}
