#pragma once
#include <string>

namespace map {

    enum class Side {
        Bid,
        Ask
    };

    inline std::string toString(Side s) {
        return (s == Side::Bid) ? "Bid" : "Ask";
    }

    inline Side opposite(Side s) {
        return (s == Side::Bid) ? Side::Ask : Side::Bid;
    }

} // namespace map
