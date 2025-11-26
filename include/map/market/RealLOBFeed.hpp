#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>   // for std::sin

namespace map {

// One snapshot of the real limit order book (aggregated)
struct RealSnapshot {
    double mid        = 0.0;   // midpoint price
    double spread     = 0.0;   // best ask - best bid
    double bidDepth15 = 0.0;   // sum of bids_market_notional_1..15
    double askDepth15 = 0.0;   // sum of asks_market_notional_1..15
};

// Simple CSV split (no quoted fields, fine for numeric CSV)
inline std::vector<std::string> splitCSVLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::stringstream ss(line);
    while (std::getline(ss, cell, ',')) {
        out.push_back(cell);
    }
    return out;
}

class RealLOBFeed {
public:
    RealLOBFeed() = default;

    explicit RealLOBFeed(const std::string& filename, bool shockVol = false) {
        loadCSV(filename, shockVol);
    }

    // NOTE: second param = stress flag for --shock-vol
    bool loadCSV(const std::string& filename, bool shockVol = false) {
        snapshots_.clear();

        // Reserve to avoid reallocations (BTC_1sec ≈ 1.03M rows)
        snapshots_.reserve(1'200'000);

        std::ifstream in(filename);
        if (!in.is_open()) {
            std::cerr << "RealLOBFeed: failed to open file " << filename << "\n";
            return false;
        }

        std::string headerLine;
        if (!std::getline(in, headerLine)) {
            std::cerr << "RealLOBFeed: empty file " << filename << "\n";
            return false;
        }

        auto headers = splitCSVLine(headerLine);
        if (headers.empty()) {
            std::cerr << "RealLOBFeed: no headers in file " << filename << "\n";
            return false;
        }

        int midIdx    = -1;
        int spreadIdx = -1;
        std::vector<int> bidMNIdx(15, -1);
        std::vector<int> askMNIdx(15, -1);

        for (int col = 0; col < (int)headers.size(); ++col) {
            const std::string& name = headers[col];

            if (name == "midpoint") {
                midIdx = col;
            } else if (name == "spread") {
                spreadIdx = col;
            } else {
                for (int lvl = 1; lvl <= 15; ++lvl) {
                    std::string bName = "bids_market_notional_" + std::to_string(lvl);
                    std::string aName = "asks_market_notional_" + std::to_string(lvl);
                    if (name == bName) bidMNIdx[lvl - 1] = col;
                    if (name == aName) askMNIdx[lvl - 1] = col;
                }
            }
        }

        if (midIdx < 0 || spreadIdx < 0) {
            std::cerr << "RealLOBFeed: missing midpoint/spread in " << filename << "\n";
            return false;
        }

        std::size_t lineCount = 0;
        std::string line;

        while (std::getline(in, line)) {
            if (line.empty()) continue;

            auto cells = splitCSVLine(line);
            if ((int)cells.size() <= std::max(midIdx, spreadIdx)) {
                continue;
            }

            RealSnapshot snap;

            try {
                snap.mid    = std::stod(cells[midIdx]);
                snap.spread = std::stod(cells[spreadIdx]);
            } catch (...) {
                continue;
            }

            double bidSum = 0.0;
            double askSum = 0.0;

            for (int i = 0; i < 15; ++i) {
                if (bidMNIdx[i] >= 0 && bidMNIdx[i] < (int)cells.size()) {
                    try { bidSum += std::stod(cells[bidMNIdx[i]]); } catch (...) {}
                }
                if (askMNIdx[i] >= 0 && askMNIdx[i] < (int)cells.size()) {
                    try { askSum += std::stod(cells[askMNIdx[i]]); } catch (...) {}
                }
            }

            snap.bidDepth15 = bidSum;
            snap.askDepth15 = askSum;

            // --- Stress hook: shock-vol ---
            if (shockVol) {
                // Deterministic "volatility" jiggle on mid + spread
                double phase  = static_cast<double>(lineCount) * 0.01;
                double shock  = 1.0 + 0.5 * std::sin(phase);  // in [0.5, 1.5]
                snap.mid      = snap.mid * shock;
                snap.spread   = snap.spread * (1.0 + 0.25 * std::sin(phase * 0.7));
            }

            snapshots_.push_back(snap);
            ++lineCount;
        }

        std::cout << "RealLOBFeed: loaded " << lineCount
                  << " snapshots from " << filename << "\n";

        return !snapshots_.empty();
    }

    bool valid() const { return !snapshots_.empty(); }

    const RealSnapshot& at(std::size_t idx) const {
        return snapshots_[idx % snapshots_.size()];
    }

    std::size_t size() const {
        return snapshots_.size();
    }

private:
    std::vector<RealSnapshot> snapshots_;
};

} // namespace map
