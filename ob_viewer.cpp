//
// MAP OrderBook Viewer (SFML 3 compatible)
//
// - Dark ladder-style UI
// - Bids (green) on left, asks (red) on right
// - Volume bars behind sizes
// - Stats: best bid/ask, mid, spread, orders/sec
// - Random-walk mid price + continuous random order flow
//

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <random>
#include <string>
#include <optional>
#include <cstdio>

#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/Types.hpp"

using map::OrderBook;
using map::Side;
using map::EventBus;
using map::NewOrderEvent;
using map::Logger;
using map::Price;
using map::Quantity;

static int toInt(Price p)    { return static_cast<int>(p.raw()); }
static int toInt(Quantity q) { return static_cast<int>(q.raw()); }

struct Stats {
    double lastMid       = 100.0;
    double mid           = 100.0;
    double spread        = 0.0;
    int    ordersPerSec  = 0;
    int    ordersThisSec = 0;
    sf::Clock secClock;
};

int main() {
    // ---------------------------------------------------------------------
    // Core engine objects
    // ---------------------------------------------------------------------
    EventBus bus;
    Logger   logger("events.bin");
    OrderBook book;
    Stats stats;

    // ---------------------------------------------------------------------
    // EventBus subscriptions
    // ---------------------------------------------------------------------
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        // Single-symbol book for now
        book.addOrder(e.side, e.price, e.qty);
        stats.ordersThisSec++;
    });

    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        logger.log(e);
    });

    // ---------------------------------------------------------------------
    // Seed the book with some initial depth
    // ---------------------------------------------------------------------
    auto seedLevel = [&](Side side, int px, int qty) {
        NewOrderEvent e;
        e.symbol = "TEST";
        e.side   = side;
        e.price  = Price{px};
        e.qty    = Quantity{qty};
        bus.publish(e);
    };

    for (int i = 0; i < 5; ++i) {
        seedLevel(Side::Bid, 100 - i, 10 + 3 * i);
        seedLevel(Side::Ask, 101 + i,  8 + 4 * i);
    }

    // ---------------------------------------------------------------------
    // Random order flow
    // ---------------------------------------------------------------------
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> qtyDist(1, 20);
    std::normal_distribution<double>   midMove(0.0, 0.05); // small random walk

    sf::Clock flowClock;

    auto updateMidFromBook = [&]() {
        auto bb = book.bestBid();
        auto ba = book.bestAsk();
        stats.lastMid = stats.mid;
        if (bb && ba) {
            stats.mid    = 0.5 * (bb->raw() + ba->raw());
            stats.spread = ba->raw() - bb->raw();
        } else {
            stats.mid    = stats.lastMid;
            stats.spread = 0.0;
        }
    };

    auto publishRandomOrder = [&]() {
        // Random walk "market"
        stats.mid += midMove(rng);
        if (stats.mid < 1.0) stats.mid = 1.0;

        bool isBid = (sideDist(rng) == 0);
        Side side  = isBid ? Side::Bid : Side::Ask;

        int pxBase = static_cast<int>(stats.mid + 0.5);
        std::uniform_int_distribution<int> pxOffset(-3, 3);
        int px  = pxBase + pxOffset(rng);
        int qty = qtyDist(rng);

        NewOrderEvent e;
        e.symbol = "TEST";
        e.side   = side;
        e.price  = Price{px};
        e.qty    = Quantity{qty};
        bus.publish(e);

        updateMidFromBook();
    };

    updateMidFromBook();

    // ---------------------------------------------------------------------
    // SFML window / font (SFML 3 API)
    // ---------------------------------------------------------------------
    sf::RenderWindow window(
        sf::VideoMode({1100u, 650u}),
        "MAP HFT - OrderBook Viewer"
    );
    window.setFramerateLimit(60);

    sf::Font font;
    bool hasFont = font.openFromFile("assets/DejaVuSans.ttf");

    stats.secClock.restart();
    flowClock.restart();

    // ---------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------
    while (window.isOpen()) {
        // --- SFML 3 event loop (pollEvent -> std::optional<sf::Event>) ---
        while (auto event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
                break;
            }

            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Escape) {
                    window.close();
                    break;
                }
                if (key->code == sf::Keyboard::Key::Space) {
                    // Burst of orders on SPACE
                    for (int i = 0; i < 3; ++i) {
                        publishRandomOrder();
                    }
                }
            }
        }

        // --- Auto-flow: one random order every ~80ms ---
        if (flowClock.getElapsedTime().asMilliseconds() > 80) {
            flowClock.restart();
            publishRandomOrder();
        }

        // --- Orders/sec stats update ---
        if (stats.secClock.getElapsedTime().asSeconds() >= 1.0f) {
            stats.ordersPerSec  = stats.ordersThisSec;
            stats.ordersThisSec = 0;
            stats.secClock.restart();
        }

        // --- Snapshot book ---
        auto bids = book.snapshot(Side::Bid);
        auto asks = book.snapshot(Side::Ask);

        // -----------------------------------------------------------------
        // Rendering
        // -----------------------------------------------------------------
        window.clear(sf::Color(8, 10, 20)); // dark background

        float w = static_cast<float>(window.getSize().x);
        float h = static_cast<float>(window.getSize().y);
        (void)h;

        // Title
        if (hasFont) {
            sf::Text title(font);
            title.setCharacterSize(24);
            title.setFillColor(sf::Color(210, 210, 230));
            title.setString("MAP HFT - OrderBook Viewer");
            title.setPosition({20.f, 10.f});
            window.draw(title);
        }

        // Stats panel (top-right)
        if (hasFont) {
            auto bb = book.bestBid();
            auto ba = book.bestAsk();

            std::string bbStr = bb ? std::to_string(bb->raw()) : "-";
            std::string baStr = ba ? std::to_string(ba->raw()) : "-";

            char buf[256];
            std::snprintf(
                buf, sizeof(buf),
                "Best Bid: %s   Best Ask: %s\nMid: %.2f   Spread: %.2f\nOrders/sec: %d",
                bbStr.c_str(), baStr.c_str(),
                stats.mid, stats.spread,
                stats.ordersPerSec
            );

            sf::Text statsText(font);
            statsText.setCharacterSize(16);
            statsText.setFillColor(sf::Color(180, 180, 200));
            statsText.setString(buf);
            statsText.setPosition({w - 420.f, 20.f});
            window.draw(statsText);
        }

        // Ladder layout
        float ladderTop   = 90.f;
        float rowHeight   = 24.f;
        int   maxLevels   = 18;
        float centerX     = w * 0.52f;
        float bidQtyX     = centerX - 200.f;
        float priceX      = centerX - 20.f;
        float askQtyX     = centerX + 80.f;
        float barMaxWidth = 150.f;

        // Ladder header
        if (hasFont) {
            sf::Text hdr(font);
            hdr.setCharacterSize(18);
            hdr.setFillColor(sf::Color(160, 160, 200));
            hdr.setString("Bids                    Price                    Asks");
            hdr.setPosition({bidQtyX - 20.f, ladderTop - 30.f});
            window.draw(hdr);
        }

        // Max qty for bar scaling
        int maxQty = 1;
        auto updateMax = [&](const std::vector<map::LevelInfo>& lvls) {
            for (const auto& lvl : lvls) {
                maxQty = std::max(maxQty, toInt(lvl.totalQty));
            }
        };
        updateMax(bids);
        updateMax(asks);

        // Draw a single ladder row
        auto drawLevelRow = [&](const map::LevelInfo* bidLvl,
                                const map::LevelInfo* askLvl,
                                float y) {
            // Volume bars
            if (bidLvl) {
                float ratio = static_cast<float>(toInt(bidLvl->totalQty)) / maxQty;
                float bw    = barMaxWidth * ratio;

                sf::RectangleShape bar;
                bar.setSize({bw, rowHeight - 3.f});
                bar.setPosition({bidQtyX + 50.f - bw, y + 2.f}); // leftwards
                bar.setFillColor(sf::Color(0, 160, 0, 120));
                window.draw(bar);
            }
            if (askLvl) {
                float ratio = static_cast<float>(toInt(askLvl->totalQty)) / maxQty;
                float bw    = barMaxWidth * ratio;

                sf::RectangleShape bar;
                bar.setSize({bw, rowHeight - 3.f});
                bar.setPosition({askQtyX - 10.f, y + 2.f}); // rightwards
                bar.setFillColor(sf::Color(200, 40, 40, 120));
                window.draw(bar);
            }

            if (!hasFont) return;

            // Choose a single price to display
            std::optional<Price> rowPrice;

            // Bid qty
            if (bidLvl) {
                sf::Text t(font);
                t.setCharacterSize(18);
                t.setFillColor(sf::Color(0, 220, 0));
                t.setString("+" + std::to_string(toInt(bidLvl->totalQty)));
                t.setPosition({bidQtyX, y});
                window.draw(t);

                rowPrice = bidLvl->price;
            }

            // Ask qty
            if (askLvl) {
                sf::Text t(font);
                t.setCharacterSize(18);
                t.setFillColor(sf::Color(230, 80, 80));
                t.setString("-" + std::to_string(toInt(askLvl->totalQty)));
                t.setPosition({askQtyX, y});
                window.draw(t);

                if (!rowPrice) {
                    rowPrice = askLvl->price;
                }
            }

            // Single price for the row
            if (rowPrice) {
                sf::Text p(font);
                p.setCharacterSize(18);
                p.setFillColor(sf::Color(200, 200, 220));
                p.setString(std::to_string(toInt(*rowPrice)));
                p.setPosition({priceX, y});
                window.draw(p);
            }
        };

        int bidCount = static_cast<int>(bids.size());
        int askCount = static_cast<int>(asks.size());
        int rows     = std::min(maxLevels, std::max(bidCount, askCount));

        for (int i = 0; i < rows; ++i) {
            const map::LevelInfo* bidLvl = (i < bidCount) ? &bids[i] : nullptr;
            const map::LevelInfo* askLvl = (i < askCount) ? &asks[i] : nullptr;
            float y = ladderTop + static_cast<float>(i) * rowHeight;
            drawLevelRow(bidLvl, askLvl, y);
        }

        // Horizontal separator line
        {
            sf::RectangleShape line;
            line.setSize({w - 40.f, 1.0f});
            line.setPosition({20.f, ladderTop - 8.f});
            line.setFillColor(sf::Color(40, 40, 80));
            window.draw(line);
        }

        window.display();
    }

    return 0;
}
