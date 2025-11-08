//
// Created by Avi Maslow on 11/6/25.
//

#pragma once
#include <string>

namespace map {

    enum class Side {
        Bid,
        Ask
    };

    // Optional helper utilities
    inline std::string toString(Side s) {
        return (s == Side::Bid) ? "Bid" : "Ask";
    }

    inline Side opposite(Side s) {
        return (s == Side::Bid) ? Side::Ask : Side::Bid;
    }

} // namespace map
E_H