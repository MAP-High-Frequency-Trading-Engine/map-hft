#include "map/risk/RiskLimits.hpp"
#include "map/Side.hpp"
#include "map/Types.hpp"

#include <cmath>
#include <iostream>
#include <cstdint>

namespace map {

    // Check if an order is allowed by simple max-qty / max-position rules.
    bool RiskLimits::checkOrder(const std::string& symbol, Side side, Quantity qty) {
        // 0. Basic sanity
        if (qty.raw() <= 0) {
            std::cout << "RISK REJECT: non-positive qty " << qty.raw() << std::endl;
            return false;
        }

        // 1. Per-order size limit
        if (qty.raw() > maxOrderQty().raw()) {
            std::cout << "RISK REJECT: Order qty " << qty.raw()
                      << " > max " << maxOrderQty().raw() << std::endl;
            return false;
        }

        // 2. Simulate new net position for this symbol

        // Current net position (signed). Quantity is Strong<QuantityTag, int32_t>,
        // so raw() is int32_t; promote to int64_t to avoid overflow on add.
        PositionState tmp = getState(symbol);
        std::int64_t currentPos = static_cast<std::int64_t>(tmp.netQty.raw());

        // Signed qty for this order: Bid = +qty, Ask = -qty.
        std::int64_t orderSigned =
            (side == Side::Bid)
                ? static_cast<std::int64_t>(qty.raw())
                : -static_cast<std::int64_t>(qty.raw());

        std::int64_t nextPos = currentPos + orderSigned;

        // 3. Enforce max absolute net position
        std::int64_t maxPos = static_cast<std::int64_t>(maxPositionQty().raw());
        if (std::llabs(nextPos) > maxPos) {
            std::cout << "RISK REJECT: Net position " << nextPos
                      << " > max " << maxPositionQty().raw() << std::endl;
            return false;
        }

        // All checks passed
        return true;
    }

    // Update position and notional after a *trade* is executed.
    void RiskLimits::onTrade(const std::string& symbol, Side side, Quantity qty, Price price) {
        // Get or create state for this symbol.
        PositionState& st = state_[symbol];

        // 1. Update net position
        std::int64_t currentPos = static_cast<std::int64_t>(st.netQty.raw());
        std::int64_t tradeSigned =
            (side == Side::Bid)
                ? static_cast<std::int64_t>(qty.raw())
                : -static_cast<std::int64_t>(qty.raw());

        std::int64_t nextPos = currentPos + tradeSigned;

        // Store as signed int32_t inside Quantity
        st.netQty = Quantity{ static_cast<std::int32_t>(nextPos) };

        // 2. Update total notional exposure (always non-negative)
        // qty.raw(): int32_t, price.raw(): int32_t
        std::int64_t tradeNotionalLL =
            static_cast<std::int64_t>(qty.raw()) *
            static_cast<std::int64_t>(price.raw());

        // Treat exposure as magnitude
        if (tradeNotionalLL < 0) {
            tradeNotionalLL = -tradeNotionalLL;
        }

        std::uint64_t tradeNotionalAbs =
            static_cast<std::uint64_t>(tradeNotionalLL);

        st.totalNotional = Notional{
            st.totalNotional.raw() + tradeNotionalAbs
        };
    }

    // Read-only accessor: returns copy of current state for a symbol.
    PositionState RiskLimits::getState(const std::string& symbol) const {
        if (auto it = state_.find(symbol); it != state_.end()) {
            return it->second;
        }
        return PositionState{}; // zeroed
    }

} // namespace map
