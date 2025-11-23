#include "map/MarketDataFeed.hpp"

#include <sstream>
#include <iostream>

namespace map {

// -----------------------
// Constructor
// -----------------------
CSVLOBFeed::CSVLOBFeed(const std::string& symbol, const std::string& csvPath)
    : symbol_(symbol),
      in_(csvPath),
      headerSkipped_(false)
{
    if (!in_.is_open()) {
        std::cerr << "CSVLOBFeed: failed to open " << csvPath << "\n";
    }
}

// -----------------------
// Helpers
// -----------------------
std::vector<std::string> CSVLOBFeed::splitLine(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string field;

    while (std::getline(ss, field, ',')) {
        out.push_back(field);
    }
    return out;
}

double CSVLOBFeed::parseDouble(const std::vector<std::string>& cols,
                               std::size_t idx) {
    if (idx >= cols.size()) return 0.0;
    const std::string& s = cols[idx];
    if (s.empty()) return 0.0;

    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

// -----------------------
// Main reader
// -----------------------
bool CSVLOBFeed::next(LOBSnapshot& out) {
    std::string line;

    while (std::getline(in_, line)) {
        if (!headerSkipped_) {
            // First line is header
            headerSkipped_ = true;
            continue;
        }

        if (line.empty())
            continue;

        auto cols = splitLine(line);

        // Dataset has ~156 columns. Bail if row is malformed.
        if (cols.size() < 156) {
            continue;
        }

        out.symbol    = symbol_;
        out.systemTime = parseDouble(cols, 1);
        out.midpoint   = parseDouble(cols, 2);
        out.spread     = parseDouble(cols, 3);
        out.buys       = parseDouble(cols, 4);
        out.sells      = parseDouble(cols, 5);

        // Indices from your comment:
        //
        //  6–20   : bids_distance_0..14
        // 51–65   : bids_limit_notional_0..14
        // 81–95   : asks_distance_0..14
        // 126–140 : asks_limit_notional_0..14

        // Bid distances & limit notionals
        for (std::size_t i = 0; i < LOBSnapshot::NUM_LEVELS; ++i) {
            out.bidDistance[i]      = parseDouble(cols,  6 + i);
            out.bidLimitNotional[i] = parseDouble(cols, 51 + i);
        }

        // Ask distances & limit notionals
        for (std::size_t i = 0; i < LOBSnapshot::NUM_LEVELS; ++i) {
            out.askDistance[i]      = parseDouble(cols,  81 + i);
            out.askLimitNotional[i] = parseDouble(cols, 126 + i);
        }

        return true;
    }

    return false; // EOF
}

} // namespace map
