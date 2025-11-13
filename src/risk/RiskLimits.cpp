#include "map/risk/RiskLimits.hpp"

#include <algorithm> // std::max, std::min
#include <cstdint>

namespace map {

RiskLimits::RiskLimits(RiskConfig cfg)
    : cfg_(cfg) {}

const PositionState* RiskLimits::findPosition(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return nullptr;
    return &it->second;
}

PositionState& RiskLimits::getOrCreate(const std::string& symbol) {
    return positions_[symbol]; // default-constructed PositionState
}

Notional RiskLimits::computeOrderNotional(Price price, Quantity qty) const {
    // If you have an operator*(Price, Quantity) → Notional, you can use that.
    // Here we do it via raw() to be safe with your strong typedefs.
    auto p = static_cast<std::int64_t>(price.raw());
    auto q = static_cast<std::int64_t>(qty.raw());
    auto n = p * q;
    return Notional{n};
}

bool RiskLimits::checkOrder(
    const std::string& symbol,
    Side side,
    Price price,
    Quantity qty
) const {
    // 1) Basic qty sanity
    if (qty.raw() <= 0) {
        return false;
    }
    if (qty.raw() > cfg_.maxOrderSize.raw()) {
        return false;
    }

    // 2) Simulate impact on position + notional
    const PositionState* current = findPosition(symbol);
    PositionState tmp{};
    if (current) {
        tmp = *current;
    }

    auto signedQty = static_cast<std::int64_t>(qty.raw());
    if (side == Side::Bid) {
        tmp.netPosition = Quantity{tmp.netPosition.raw() + signedQty};
    } else { // Side::Ask
        tmp.netPosition = Quantity{tmp.netPosition.raw() - signedQty};
    }

    Notional orderNotional = computeOrderNotional(price, qty);
    tmp.netNotional = Notional{tmp.netNotional.raw() + orderNotional.raw()};


    // 3) Apply limits
    auto posRaw = tmp.netPosition.raw();
    if ((posRaw < 0 ? -posRaw : posRaw) > cfg_.maxPosition.raw()) {
        return false;
    }
    if (tmp.netNotional.raw() > cfg_.maxNotional.raw()) {
        return false;
    }

    return true;
}


void RiskLimits::onTrade(
    const std::string& symbol,
    Side side,
    Price price,
    Quantity qty
) {
    auto& st = getOrCreate(symbol);

    auto signedQty = static_cast<std::int64_t>(qty.raw());
    if (side == Side::Bid) {
        st.netPosition = Quantity{st.netPosition.raw() + signedQty};
    } else { // Side::Ask
        st.netPosition = Quantity{st.netPosition.raw() - signedQty};
    }

    Notional n = computeOrderNotional(price, qty);
    st.netNotional = Notional{st.netNotional.raw() + n.raw()};
}

} // namespace map
