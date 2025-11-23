#pragma once

#include <array>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>
// include/map/MarketDataFeed.hpp

namespace map {

    struct LOBSnapshot {
        static constexpr std::size_t NUM_LEVELS = 15;

        std::string symbol;
        double      systemTime{};
        double      midpoint{};
        double      spread{};
        double      buys{};
        double      sells{};

        std::array<double, NUM_LEVELS> bidDistance{};
        std::array<double, NUM_LEVELS> askDistance{};
        std::array<double, NUM_LEVELS> bidLimitNotional{};
        std::array<double, NUM_LEVELS> askLimitNotional{};

        double bestBidPrice() const {
            if (midpoint <= 0.0) return 0.0;
            double d = bidDistance[0] / 100.0; // % → fraction
            return midpoint * (1.0 + d);       // bids: d should be negative
        }

        double bestAskPrice() const {
            if (midpoint <= 0.0) return 0.0;
            double d = askDistance[0] / 100.0; // % → fraction
            return midpoint * (1.0 + d);       // asks: d positive
        }
    };

    class CSVLOBFeed {
    public:
        CSVLOBFeed(const std::string& symbol, const std::string& csvPath);

        bool good() const { return in_.good(); }
        bool next(LOBSnapshot& out);

    private:
        std::string   symbol_;
        std::ifstream in_;
        bool          headerSkipped_ = false;

        static std::vector<std::string> splitLine(const std::string& line);
        static double parseDouble(const std::vector<std::string>& cols,
                                  std::size_t idx);
    };

} // namespace map
