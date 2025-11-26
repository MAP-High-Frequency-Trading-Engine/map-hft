#pragma once

#include <array>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

namespace map {

    /**
     * One snapshot of the real limit order book from the Kaggle CSV.
     *
     * The CSV is assumed to have:
     *   - midpoint
     *   - spread
     *   - buys / sells counts
     *   - bids_distance_0..14
     *   - bids_limit_notional_0..14
     *   - asks_distance_0..14
     *   - asks_limit_notional_0..14
     */
    struct LOBSnapshot {
        static constexpr std::size_t NUM_LEVELS = 15;

        std::string symbol;     // which asset (BTC / ETH / ADA)
        double      systemTime{};  // timestamp from CSV (if present)
        double      midpoint{};    // "midpoint" column
        double      spread{};      // "spread" column
        double      buys{};        // number of buy trades in window
        double      sells{};       // number of sell trades in window

        std::array<double, NUM_LEVELS> bidDistance{};      // bids_distance_0..14
        std::array<double, NUM_LEVELS> askDistance{};      // asks_distance_0..14
        std::array<double, NUM_LEVELS> bidLimitNotional{}; // bids_limit_notional_0..14
        std::array<double, NUM_LEVELS> askLimitNotional{}; // asks_limit_notional_0..14

        // Approximate best bid from midpoint + distance
        double bestBidPrice() const {
            if (midpoint <= 0.0) return 0.0;
            double d = bidDistance[0] / 100.0;   // percentage → fraction
            return midpoint * (1.0 + d);        // bids: d typically negative
        }

        // Approximate best ask from midpoint + distance
        double bestAskPrice() const {
            if (midpoint <= 0.0) return 0.0;
            double d = askDistance[0] / 100.0;   // percentage → fraction
            return midpoint * (1.0 + d);        // asks: d positive
        }
    };

    /**
     * CSVLOBFeed:
     *   - Reads a Kaggle LOB CSV file for a single symbol.
     *   - Each call to next(out) fills a LOBSnapshot with:
     *       midpoint, spread, buys, sells,
     *       bids_distance_0..14, bids_limit_notional_0..14,
     *       asks_distance_0..14, asks_limit_notional_0..14.
     */
    class CSVLOBFeed {
    public:
        CSVLOBFeed(const std::string& symbol, const std::string& csvPath);

        bool good() const { return in_.good(); }

        /**
         * Read the next valid row into `out`.
         * Returns true on success, false on EOF.
         */
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
