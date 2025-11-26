// src/OrderBook.cpp
#include "map/OrderBook.hpp"

#include <algorithm>
#include <numeric>
#include <functional>
#include <cassert>

namespace map {

// ------------------------------------------------------------
// Constructor
// ------------------------------------------------------------

OrderBook::OrderBook(
    RiskLimits* risk
#ifdef MAP_USE_PMR
    , std::pmr::memory_resource* mr
#endif
)
    : risk_(risk)
#ifdef MAP_USE_PMR
    , buffer_(mr ? mr : std::pmr::get_default_resource())
    , books_(&buffer_)
    , index_(&buffer_)
#endif
{
}

// ------------------------------------------------------------
// Helpers: per-symbol book access
// ------------------------------------------------------------

OrderBook::PerSymbolBook* OrderBook::getOrCreateBook(const std::string& symbol) {
    auto it = books_.find(symbol);
    if (it != books_.end()) {
        return &it->second;
    }

#ifdef MAP_USE_PMR
    auto [insIt, _] = books_.emplace(symbol, PerSymbolBook(&buffer_));
    return &insIt->second;
#else
    auto [insIt, _] = books_.emplace(symbol, PerSymbolBook{});
    return &insIt->second;
#endif
}

const OrderBook::PerSymbolBook* OrderBook::getBook(const std::string& symbol) const {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        return nullptr;
    }
    return &it->second;
}

const OrderBook::PerSymbolBook* OrderBook::getFirstBook() const {
    if (books_.empty()) {
        return nullptr;
    }
    return &books_.begin()->second;
}

std::string OrderBook::getFirstSymbol() const {
    if (books_.empty()) {
        return {};
    }
    return books_.begin()->first;
}

// ------------------------------------------------------------
// Matching helpers
// ------------------------------------------------------------

// Match an incoming BID against lowest asks
static void matchBid(
    const std::string& symbol,
    Price              px,
    Quantity&          qtyRemaining,
    OrderBook::PerSymbolBook& book,
    RiskLimits*        risk
) {
    auto& asks = book.asks;

    while (qtyRemaining.raw() > 0 && !asks.empty()) {
        auto levelIt = asks.begin();              // lowest ask
        Price bestAsk = levelIt->first;

        if (px.raw() < bestAsk.raw()) {
            // No more crossing levels
            break;
        }

        auto& q = levelIt->second;                // LevelQueue

        auto it = q.begin();
        while (it != q.end() && qtyRemaining.raw() > 0) {
            Order& resting = *it;

            int avail = resting.qty.raw();
            int need  = qtyRemaining.raw();
            int traded = std::min(avail, need);

            Quantity tradeQty{ static_cast<std::int32_t>(traded) };

            // Risk update for both sides
            if (risk) {
                // Incoming side: we are BID
                risk->onTrade(symbol, Side::Bid, tradeQty, bestAsk);
                // Resting side: they are ASK
                risk->onTrade(symbol, Side::Ask, tradeQty, bestAsk);
            }

            resting.qty = Quantity{ avail - traded };
            qtyRemaining = Quantity{ need - traded };

            if (resting.qty.raw() <= 0) {
                it = q.erase(it);
            } else {
                ++it;
            }
        }

        if (q.empty()) {
            asks.erase(levelIt);
        }
    }
}

// Match an incoming ASK against highest bids
static void matchAsk(
    const std::string& symbol,
    Price              px,
    Quantity&          qtyRemaining,
    OrderBook::PerSymbolBook& book,
    RiskLimits*        risk
) {
    auto& bids = book.bids;

    while (qtyRemaining.raw() > 0 && !bids.empty()) {
        auto levelIt = bids.begin();             // highest bid (std::greater)
        Price bestBid = levelIt->first;

        if (px.raw() > bestBid.raw()) {
            // No more crossing levels
            break;
        }

        auto& q = levelIt->second;               // LevelQueue

        auto it = q.begin();
        while (it != q.end() && qtyRemaining.raw() > 0) {
            Order& resting = *it;

            int avail = resting.qty.raw();
            int need  = qtyRemaining.raw();
            int traded = std::min(avail, need);

            Quantity tradeQty{ static_cast<std::int32_t>(traded) };

            // Risk update for both sides
            if (risk) {
                // Incoming side: we are ASK
                risk->onTrade(symbol, Side::Ask, tradeQty, bestBid);
                // Resting side: they are BID
                risk->onTrade(symbol, Side::Bid, tradeQty, bestBid);
            }

            resting.qty = Quantity{ avail - traded };
            qtyRemaining = Quantity{ need - traded };

            if (resting.qty.raw() <= 0) {
                it = q.erase(it);
            } else {
                ++it;
            }
        }

        if (q.empty()) {
            bids.erase(levelIt);
        }
    }
}

// ------------------------------------------------------------
// addOrder / cancelOrder
// ------------------------------------------------------------

OrderId OrderBook::addOrder(Side side,
                            Price px,
                            Quantity qty,
                            const std::string& symbol)
{
    if (qty.raw() <= 0) {
        // RiskLimits::checkOrder will also reject, but we can guard here too
        return OrderId{0};
    }

    // Check basic risk constraints on *this* incoming order
    if (risk_ && !risk_->checkOrder(symbol, side, qty)) {
        return OrderId{0};
    }

    PerSymbolBook* book = getOrCreateBook(symbol);
    if (!book) {
        return OrderId{0};
    }

    // Incoming order will get an ID, even if fully matched (no resting qty)
    OrderId newId{ nextId_++ };
    Quantity remaining = qty;

    // 1) Attempt to match aggressively against opposite side
    if (side == Side::Bid) {
        matchBid(symbol, px, remaining, *book, risk_);
    } else {
        matchAsk(symbol, px, remaining, *book, risk_);
    }

    // 2) If anything left, rest it in the book at this price
    if (remaining.raw() > 0) {
        Order o;
        o.id     = newId;
        o.symbol = symbol;
        o.side   = side;
        o.px     = px;
        o.qty    = remaining;

        if (side == Side::Bid) {
            book->bids[px].push_back(o);
        } else {
            book->asks[px].push_back(o);
        }

        index_.emplace(newId, std::make_tuple(symbol, side, px));
    }

    return newId;
}

bool OrderBook::cancelOrder(OrderId id)
{
    auto it = index_.find(id);
    if (it == index_.end()) {
        return false;
    }

    const std::string& symbol = std::get<0>(it->second);
    Side               side   = std::get<1>(it->second);
    Price              px     = std::get<2>(it->second);

    PerSymbolBook* book = getOrCreateBook(symbol);
    if (!book) {
        index_.erase(it);
        return false;
    }

    if (side == Side::Bid) {
        auto levelIt = book->bids.find(px);
        if (levelIt == book->bids.end()) {
            index_.erase(it);
            return false;
        }
        LevelQueue& q = levelIt->second;

        for (auto qIt = q.begin(); qIt != q.end(); ++qIt) {
            if (qIt->id == id) {
                q.erase(qIt);
                if (q.empty()) {
                    book->bids.erase(levelIt);
                }
                index_.erase(it);
                return true;
            }
        }
    } else {
        auto levelIt = book->asks.find(px);
        if (levelIt == book->asks.end()) {
            index_.erase(it);
            return false;
        }
        LevelQueue& q = levelIt->second;

        for (auto qIt = q.begin(); qIt != q.end(); ++qIt) {
            if (qIt->id == id) {
                q.erase(qIt);
                if (q.empty()) {
                    book->asks.erase(levelIt);
                }
                index_.erase(it);
                return true;
            }
        }
    }

    index_.erase(it);
    return false;
}

// ------------------------------------------------------------
// Best bid / ask
// ------------------------------------------------------------

std::optional<Price> OrderBook::bestBid(const std::string& symbol) const {
    const PerSymbolBook* book = getBook(symbol);
    if (!book || book->bids.empty()) {
        return std::nullopt;
    }
    // bids: highest price first
    return book->bids.begin()->first;
}

std::optional<Price> OrderBook::bestAsk(const std::string& symbol) const {
    const PerSymbolBook* book = getBook(symbol);
    if (!book || book->asks.empty()) {
        return std::nullopt;
    }
    // asks: lowest price first
    return book->asks.begin()->first;
}

std::optional<Price> OrderBook::bestBid() const {
    std::string sym = getFirstSymbol();
    if (sym.empty()) {
        return std::nullopt;
    }
    return bestBid(sym);
}

std::optional<Price> OrderBook::bestAsk() const {
    std::string sym = getFirstSymbol();
    if (sym.empty()) {
        return std::nullopt;
    }
    return bestAsk(sym);
}

// ------------------------------------------------------------
// Snapshot (legacy: first symbol only)
// ------------------------------------------------------------

std::vector<LevelInfo> OrderBook::snapshot(Side side) const
{
    std::vector<LevelInfo> out;
    const PerSymbolBook* book = getFirstBook();
    if (!book) {
        return out;
    }

    if (side == Side::Bid) {
        for (const auto& [price, queue] : book->bids) {
            Quantity total(0);
            for (const auto& ord : queue) {
                total = Quantity(total.raw() + ord.qty.raw());
            }
            out.push_back(LevelInfo{price, total});
        }
    } else {
        for (const auto& [price, queue] : book->asks) {
            Quantity total(0);
            for (const auto& ord : queue) {
                total = Quantity(total.raw() + ord.qty.raw());
            }
            out.push_back(LevelInfo{price, total});
        }
    }

    return out;
}

// ------------------------------------------------------------
// Depth & Imbalance (per symbol)
// ------------------------------------------------------------

Quantity OrderBook::totalDepth(const std::string& symbol,
                               Side side,
                               int maxLevels) const
{
    const PerSymbolBook* book = getBook(symbol);
    if (!book) {
        return Quantity(0);
    }

    Quantity total(0);

    if (side == Side::Bid) {
        int levels = 0;
        for (const auto& [price, queue] : book->bids) {
            for (const auto& ord : queue) {
                total = Quantity(total.raw() + ord.qty.raw());
            }
            if (maxLevels > 0 && ++levels >= maxLevels) {
                break;
            }
        }
    } else {
        int levels = 0;
        for (const auto& [price, queue] : book->asks) {
            for (const auto& ord : queue) {
                total = Quantity(total.raw() + ord.qty.raw());
            }
            if (maxLevels > 0 && ++levels >= maxLevels) {
                break;
            }
        }
    }

    return total;
}

double OrderBook::orderImbalance(const std::string& symbol,
                                 int maxLevels) const
{
    const PerSymbolBook* book = getBook(symbol);
    if (!book) {
        return 0.0;
    }

    double bidSum = 0.0;
    {
        int levels = 0;
        for (const auto& [price, queue] : book->bids) {
            for (const auto& ord : queue) {
                bidSum += static_cast<double>(ord.qty.raw());
            }
            if (maxLevels > 0 && ++levels >= maxLevels) {
                break;
            }
        }
    }

    double askSum = 0.0;
    {
        int levels = 0;
        for (const auto& [price, queue] : book->asks) {
            for (const auto& ord : queue) {
                askSum += static_cast<double>(ord.qty.raw());
            }
            if (maxLevels > 0 && ++levels >= maxLevels) {
                break;
            }
        }
    }

    double denom = bidSum + askSum;
    if (denom <= 0.0) {
        return 0.0;
    }
    return (bidSum - askSum) / denom; // in [-1, 1]
}

// ------------------------------------------------------------
// Legacy depth / imbalance (first symbol)
// ------------------------------------------------------------

Quantity OrderBook::totalDepth(Side side, int maxLevels) const {
    std::string sym = getFirstSymbol();
    if (sym.empty()) {
        return Quantity(0);
    }
    return totalDepth(sym, side, maxLevels);
}

double OrderBook::orderImbalance(int maxLevels) const {
    std::string sym = getFirstSymbol();
    if (sym.empty()) {
        return 0.0;
    }
    return orderImbalance(sym, maxLevels);
}

// ------------------------------------------------------------
// Checksum
// ------------------------------------------------------------

std::uint64_t OrderBook::checksum() const {
    // Simple FNV-1a style hash over all orders in all symbols.
    std::uint64_t h = 1469598103934665603ull;

    auto fnv_mix = [&h](std::uint64_t x) {
        h ^= x;
        h *= 1099511628211ull;
    };

    for (const auto& [symbol, book] : books_) {
        for (unsigned char c : symbol) {
            fnv_mix(static_cast<std::uint64_t>(c));
        }

        for (const auto& [px, queue] : book.bids) {
            fnv_mix(static_cast<std::uint64_t>(px.raw()));
            for (const auto& o : queue) {
                fnv_mix(static_cast<std::uint64_t>(o.id.raw()));
                fnv_mix(static_cast<std::uint64_t>(o.qty.raw()));
            }
        }

        for (const auto& [px, queue] : book.asks) {
            fnv_mix(static_cast<std::uint64_t>(px.raw()));
            for (const auto& o : queue) {
                fnv_mix(static_cast<std::uint64_t>(o.id.raw()));
                fnv_mix(static_cast<std::uint64_t>(o.qty.raw()));
            }
        }
    }

    return h;
}

} // namespace map
