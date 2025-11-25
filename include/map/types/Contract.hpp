#pragma once

#include <optional>
#include <string>

namespace map::types {

struct OptionContract {
    std::string symbol;       // OCC-style option symbol (e.g., "SPY251125C00600000")
    double      strike{0.0};  // Strike price
    std::string expiration;   // "YYYY-MM-DD" in ET
    bool        isCall{true}; // true = Call, false = Put
    std::optional<int> openInterest;
};

} // namespace map::types
