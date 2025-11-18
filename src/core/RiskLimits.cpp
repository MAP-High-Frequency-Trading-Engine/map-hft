#include "map/core/RiskLimits.hpp"

#include <algorithm>
#include <cstdint>

namespace map {

const PositionState* RiskLimits::findPosition(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return nullptr;
    return &it->second;
}

PositionState& RiskLimits::getOrCreate(const std::string& symbol) {
    // Creates default-constructed PositionState if symbol not present
    return positions_[symbol];
}

Notional RiskLimits::computeOrderNotional(Price price, Quantity qty) const {
    // Multiply in a signed type to avoid overflow surprises
    auto p = static_cast<std::int64_t>(price.raw());
    auto q = static_cast<std::int64_t>(qty.raw());
    auto n = p * q;

    // Notional's underlying type is unsigned (compiler told us),
    // so cast explicitly to silence narrowing warnings.
    return Notional{ static_cast<unsigned long long>(n) };
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
    if (qty.raw() > config_.maxOrderSize.raw()) {
        return false;
    }

    // 2) Simulate impact on position + notional for this symbol
    const PositionState* current = findPosition(symbol);
    PositionState tmp{};
    if (current) {
        tmp = *current;
    }

    auto signedQty = static_cast<std::int64_t>(qty.raw());
    if (side == Side::Bid) {
        tmp.netPosition = Quantity{ tmp.netPosition.raw() + signedQty };
    } else { // Side::Ask
        tmp.netPosition = Quantity{ tmp.netPosition.raw() - signedQty };
    }

    Notional orderNotional = computeOrderNotional(price, qty);
    tmp.netNotional = Notional{
        tmp.netNotional.raw() + orderNotional.raw()
    };

    // 3) Apply limits: max |netPosition| and max notional
    auto posRaw = tmp.netPosition.raw();
    auto absPos = (posRaw < 0 ? -posRaw : posRaw);
    if (absPos > config_.maxPosition.raw()) {
        return false;
    }
    if (tmp.netNotional.raw() > config_.maxNotional.raw()) {
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
        st.netPosition = Quantity{ st.netPosition.raw() + signedQty };
    } else { // Side::Ask
        st.netPosition = Quantity{ st.netPosition.raw() - signedQty };
    }

    Notional n = computeOrderNotional(price, qty);
    st.netNotional = Notional{
        st.netNotional.raw() + n.raw()
    };
}

} // namespace map
