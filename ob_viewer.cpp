//
// MAP OrderBook Viewer
//

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <random>
#include <string>

#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/core/Event.hpp"
#include "map/core/EventBus.hpp"
#include "map/core/Logger.hpp"
#include "map/Types.hpp"      // <--- ADD THIS

// Bring engine types into scope
using map::OrderBook;
using map::Side;
using map::EventBus;
using map::NewOrderEvent;
using map::Logger;
using map::Price;             // <--- ADD THIS
using map::Quantity;          // <--- AND THIS

// Price / Quantity are strong typedefs
static int toInt(Price p)    { return static_cast<int>(p.raw()); }
static int toInt(Quantity q) { return static_cast<int>(q.raw()); }


int main() {
    // ---------------------------------------------------------------------
    // Core engine objects
    // ---------------------------------------------------------------------
    EventBus bus;
    Logger   logger("events.bin");
    OrderBook book;

    // OrderBook listens for NewOrderEvent
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        // single symbol for now, ignore e.symbol
        book.addOrder(e.side, e.price, e.qty);
    });

    // Logger also listens for NewOrderEvent
    bus.subscribe<NewOrderEvent>([&](const NewOrderEvent& e) {
        logger.log(e);
    });

    // ---------------------------------------------------------------------
    // Seed book with some initial orders via events
    // ---------------------------------------------------------------------
    {
        NewOrderEvent e1;
        e1.symbol = "TEST";
        e1.side   = Side::Bid;
        e1.price  = Price{100};
        e1.qty    = Quantity{10};

        NewOrderEvent e2;
        e2.symbol = "TEST";
        e2.side   = Side::Bid;
        e2.price  = Price{99};
        e2.qty    = Quantity{5};

        NewOrderEvent e3;
        e3.symbol = "TEST";
        e3.side   = Side::Bid;
        e3.price  = Price{98};
        e3.qty    = Quantity{7};

        NewOrderEvent e4;
        e4.symbol = "TEST";
        e4.side   = Side::Ask;
        e4.price  = Price{101};
        e4.qty    = Quantity{4};

        NewOrderEvent e5;
        e5.symbol = "TEST";
        e5.side   = Side::Ask;
        e5.price  = Price{102};
        e5.qty    = Quantity{9};

        NewOrderEvent e6;
        e6.symbol = "TEST";
        e6.side   = Side::Ask;
        e6.price  = Price{103};
        e6.qty    = Quantity{3};

        bus.publish(e1);
        bus.publish(e2);
        bus.publish(e3);
        bus.publish(e4);
        bus.publish(e5);
        bus.publish(e6);
    }

    // ---------------------------------------------------------------------
    // Simple RNG for random orders (auto-sim + SPACE bar)
    // ---------------------------------------------------------------------
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> priceOffsetDist(-3, 3);
    std::uniform_int_distribution<int> qtyDist(1, 10);

    auto publishRandomOrder = [&](bool aroundMid) {
        bool isBid = (sideDist(rng) == 0);
        Side side  = isBid ? Side::Bid : Side::Ask;

        int pxBase = 100;
        if (aroundMid) {
            // If we have best bid/ask, center around mid instead of 100
            auto bb = book.bestBid();
            auto ba = book.bestAsk();
            if (bb && ba) {
                pxBase = static_cast<int>((bb->raw() + ba->raw()) / 2);
            }
        }

        int px  = pxBase + priceOffsetDist(rng);
        int qty = qtyDist(rng);

        NewOrderEvent e;
        e.symbol = "TEST";
        e.side   = side;
        e.price  = Price{px};
        e.qty    = Quantity{qty};

        bus.publish(e);
    };

    // ---------------------------------------------------------------------
    // SFML window + drawing state
    // ---------------------------------------------------------------------
    sf::RenderWindow window;
    window.create(
        sf::VideoMode({1000u, 600u}),
        sf::String("MAP OrderBook Viewer")
    );
    window.setFramerateLimit(60);

    sf::Font font;
    bool hasFont = font.openFromFile("assets/DejaVuSans.ttf");

    // For auto-simulation of random orders
    sf::Clock simClock;

    // For moving sweep line
    float sweepX = 0.0f;
    sf::Clock frameClock;

    // ---------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------
    while (window.isOpen()) {
        // --- handle events (close, keyboard) ---
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) {
                window.close();
            }

            // SPACE => manually inject a random order
            if (auto key = ev->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Space) {
                    publishRandomOrder(true);
                }
            }
        }

        // --- auto-sim: every 0.4 seconds add a random order ---
        if (simClock.getElapsedTime().asMilliseconds() > 400) {
            simClock.restart();
            publishRandomOrder(true);
        }

        // --- update sweep line position ---
        float w = static_cast<float>(window.getSize().x);
        float h = static_cast<float>(window.getSize().y);

        float dt = frameClock.restart().asSeconds();
        const float sweepSpeed = 150.0f; // pixels per second
        sweepX += sweepSpeed * dt;
        if (sweepX > w) {
            sweepX = 0.0f;
        }

        // --- clear and snapshot the book ---
        window.clear(sf::Color(5, 5, 15));

        auto bids = book.snapshot(Side::Bid);
        auto asks = book.snapshot(Side::Ask);

        if (hasFont) {
            float y          = 40.0f;
            float lineHeight = 32.0f;

            // Header row
            {
                sf::Text header(font);
                header.setCharacterSize(22);
                header.setFillColor(sf::Color(180, 180, 220));
                header.setString("Side   Price      Qty");
                header.setPosition(sf::Vector2f(60.f, y));
                window.draw(header);
                y += lineHeight;
            }

            // Best bid/ask, mid, spread
            auto bestBid = book.bestBid();
            auto bestAsk = book.bestAsk();

            if (bestBid && bestAsk) {
                double mid    = (bestBid->raw() + bestAsk->raw()) / 2.0;
                double spread = bestAsk->raw() - bestBid->raw();

                sf::Text midTxt(font);
                midTxt.setCharacterSize(18);
                midTxt.setFillColor(sf::Color(150, 150, 200));
                midTxt.setString(
                    "Mid: " + std::to_string(mid) +
                    "    Spread: " + std::to_string(spread)
                );
                midTxt.setPosition(sf::Vector2f(60.f, y));
                window.draw(midTxt);

                // Corner display
                sf::Text corner(font);
                corner.setCharacterSize(22);
                corner.setFillColor(sf::Color(210, 210, 230));

                std::string cs = "Best Bid: " + std::to_string(bestBid->raw()) +
                                 "    Best Ask: " + std::to_string(bestAsk->raw());
                corner.setString(cs);
                corner.setPosition(sf::Vector2f(w - 420.f, 10.f));
                window.draw(corner);
            }

            y += lineHeight; // move below mid/spread

            // --- Bids (green, +qty) ---
            for (const auto& lvl : bids) {
                sf::Text row(font);
                row.setCharacterSize(26);
                row.setFillColor(sf::Color(0, 255, 0)); // green

                std::string s = "B   ";
                s += std::to_string(toInt(lvl.price));
                s += "    +";
                s += std::to_string(toInt(lvl.totalQty));

                row.setString(s);
                row.setPosition(sf::Vector2f(60.f, y));
                window.draw(row);

                y += lineHeight;
            }

            // gap
            y += lineHeight * 0.5f;

            // --- Asks (red, -qty) ---
            for (const auto& lvl : asks) {
                sf::Text row(font);
                row.setCharacterSize(26);
                row.setFillColor(sf::Color(255, 60, 60)); // red

                std::string s = "A   ";
                s += std::to_string(toInt(lvl.price));
                s += "    -";
                s += std::to_string(toInt(lvl.totalQty));

                row.setString(s);
                row.setPosition(sf::Vector2f(60.f, y));
                window.draw(row);

                y += lineHeight;
            }
        }
/*
        // --- Static divider at ~35% width ---
        {
            sf::RectangleShape midLine;
            midLine.setSize(sf::Vector2f(1.0f, h));
            midLine.setPosition(sf::Vector2f(w * 0.35f, 0.f));
            midLine.setFillColor(sf::Color(40, 40, 80));
            window.draw(midLine);
        }
*/
        // --- Moving sweep line across the whole screen ---
        {
            sf::RectangleShape sweepLine;
            sweepLine.setSize(sf::Vector2f(1.0f, h));
            sweepLine.setPosition(sf::Vector2f(sweepX, 0.f));
            sweepLine.setFillColor(sf::Color(80, 80, 140));
            window.draw(sweepLine);
        }

        window.display();
    }

    return 0;
}
