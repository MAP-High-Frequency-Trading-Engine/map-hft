//
// MAP OrderBook Viewer (SFML 3 compatible) - Replay stream version
//
// - Reads events.bin via LogReader::readNext()
// - Streams NewOrder/Cancel events into OrderBook over time
// - Bids (green) on left, asks (red) on right
// - Shows best bid/ask, mid, spread, orders/sec, replay checksum, risk panel
// - Plus: speed controls, pause/step, hover tooltips, order-size histogram,
//         and a timeline scrubber for replay progress.
//
#include <cstdint>
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <cstdio>
#include <iostream>
#include <variant>
#include <array>

#include "map/OrderBook.hpp"
#include "map/Side.hpp"
#include "map/core/Event.hpp"
#include "map/replay/LogReader.hpp"
#include "map/risk/RiskLimits.hpp"
#include "map/Types.hpp"

using map::OrderBook;
using map::Side;
using map::NewOrderEvent;
using map::CancelOrderEvent;
using map::TradeEvent;
using map::Price;
using map::Quantity;
using map::RiskConfig;
using map::RiskLimits;
using map::LogReader;

using DecodedEvent = std::variant<NewOrderEvent, CancelOrderEvent, TradeEvent>;

static int toInt(Price p)    { return static_cast<int>(p.raw()); }
static int toInt(Quantity q) { return static_cast<int>(q.raw()); }

struct Stats {
    double       lastMid         = 100.0;
    double       mid             = 100.0;
    double       spread          = 0.0;
    int          ordersPerSec    = 0;
    int          ordersThisSec   = 0;
    std::size_t  eventsSeen      = 0;
    bool         eof             = false;
    std::uint64_t replayChecksum = 0;
    bool         checksumValid   = false;
    sf::Clock    secClock;
};

struct UIState {
    bool  paused         = false;
    float speedMultiplier = 1.0f;   // 1x, up/down arrows to adjust
    bool  stepOnce       = false;   // single-step when paused
};

struct DebugInfo {
    std::string lastEventStr;
};

int main() {
    // ---------------------------------------------------------------------
    // Risk + OrderBook
    // ---------------------------------------------------------------------
    RiskConfig cfg{
        .maxOrderSize = Quantity{1'000'000},
        .maxPosition  = Quantity{10'000'000},
        .maxNotional  = map::Notional{1'000'000'000}
    };
    RiskLimits risk(cfg);

    OrderBook book(&risk);
    Stats stats;
    UIState ui;
    DebugInfo dbg;

    // Histogram buckets for NewOrder sizes: [1-5], [6-10], [11-20], [21-50], [51+]
    std::array<int, 5> sizeBuckets{};
    auto bucketIndex = [](int qty) {
        if (qty <= 5)   return 0;
        if (qty <= 10)  return 1;
        if (qty <= 20)  return 2;
        if (qty <= 50)  return 3;
        return 4;
    };

    auto recordEventForDebug = [&](const DecodedEvent& ev) {
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, NewOrderEvent>) {
    int qraw = toInt(e.qty);
    sizeBuckets[bucketIndex(qraw)]++;
    char buf[256];
    std::snprintf(
        buf, sizeof(buf),
        "Last event: NEW | side=%s px=%lld qty=%d",
        (e.side == Side::Bid ? "Bid" : "Ask"),
        static_cast<long long>(e.price.raw()),
        qraw
    );
    dbg.lastEventStr = buf;
}
 else if constexpr (std::is_same_v<T, CancelOrderEvent>) {
                char buf[256];
                std::snprintf(
                    buf, sizeof(buf),
                    "Last event: CANCEL | id=%lld",
                    static_cast<long long>(e.id.raw())
                );
                dbg.lastEventStr = buf;
            } else if constexpr (std::is_same_v<T, TradeEvent>) {
                char buf[256];
                std::snprintf(
                    buf, sizeof(buf),
                    "Last event: TRADE | px=%lld qty=%d",
                    static_cast<long long>(e.price.raw()),
                    toInt(e.qty)
                );
                dbg.lastEventStr = buf;
            }
        }, ev);
    };

    // ---------------------------------------------------------------------
    // Load events from log (events.bin) via streaming
    // ---------------------------------------------------------------------
    const std::string filename = "events.bin";
    LogReader reader(filename);
    if (!reader.good()) {
        std::cerr << "Failed to open log file: " << filename << "\n";
        return 1;
    }

    // We also store every event in memory so we can scrub back/forward.
    std::vector<DecodedEvent> history;
    bool historyComplete = false; // set true once we reach EOF at least once

    sf::Clock playbackClock;
    const int msPerEventBase = 20; // base pacing of playback

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

    // Rebuild book + risk from scratch up to a given event index
    auto rebuildToIndex = [&](std::size_t targetIdx) {
        // Reset risk & book and stats
        risk = RiskLimits(cfg);
        book = OrderBook(&risk);
        book.setRiskLimits(&risk);

        stats.eventsSeen    = 0;
        stats.eof           = false;
        stats.replayChecksum = 0;
        stats.checksumValid = false;
        sizeBuckets = {};
        dbg.lastEventStr.clear();

        for (std::size_t i = 0; i < targetIdx && i < history.size(); ++i) {
            const DecodedEvent& ev = history[i];
            std::visit([&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, NewOrderEvent>) {
                    book.addOrder(e.side, e.price, e.qty, e.symbol);
                } else if constexpr (std::is_same_v<T, CancelOrderEvent>) {
                    book.cancelOrder(e.id);
                } else if constexpr (std::is_same_v<T, TradeEvent>) {
                    // no direct book mutation
                }
            }, ev);
            stats.eventsSeen++;
            recordEventForDebug(ev);
        }

        updateMidFromBook();

        if (historyComplete && targetIdx == history.size()) {
            stats.eof            = true;
            stats.replayChecksum = book.checksum();
            stats.checksumValid  = true;
        }
    };

    updateMidFromBook();
    stats.secClock.restart();

    // ---------------------------------------------------------------------
    // SFML window / font (SFML 3 API)
    // ---------------------------------------------------------------------
    sf::RenderWindow window(
        sf::VideoMode({1100u, 650u}),
        "MAP HFT - OrderBook Viewer (Replay)"
    );
    window.setFramerateLimit(60);

    sf::Font font;
    bool hasFont = font.openFromFile("assets/DejaVuSans.ttf");

    // ---------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------
    while (window.isOpen()) {
        // --- SFML event loop ---
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
                    // pause / resume
                    ui.paused = !ui.paused;
                }
                if (key->code == sf::Keyboard::Key::Up) {
                    ui.speedMultiplier *= 1.5f;
                    if (ui.speedMultiplier > 8.0f) ui.speedMultiplier = 8.0f;
                }
                if (key->code == sf::Keyboard::Key::Down) {
                    ui.speedMultiplier /= 1.5f;
                    if (ui.speedMultiplier < 0.25f) ui.speedMultiplier = 0.25f;
                }
                if (key->code == sf::Keyboard::Key::Right) {
                    // single-step in paused mode
                    if (ui.paused) {
                        ui.stepOnce = true;
                    }
                }
            }

            // Mouse click handling for timeline scrubber
            if (const auto* mbtn = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (mbtn->button == sf::Mouse::Button::Left) {
                    auto mousePos = window.mapPixelToCoords(
                        sf::Vector2i{mbtn->position.x, mbtn->position.y}
                    );
                    float w = static_cast<float>(window.getSize().x);
                    float h = static_cast<float>(window.getSize().y);

                    // Scrubber bar region (bottom strip)
                    float scrubYTop    = h - 40.f;
                    float scrubYBottom = h - 20.f;
                    float scrubXLeft   = 50.f;
                    float scrubXRight  = w - 50.f;

                    if (mousePos.y >= scrubYTop && mousePos.y <= scrubYBottom &&
                        mousePos.x >= scrubXLeft && mousePos.x <= scrubXRight &&
                        !history.empty()) {

                        float t = (mousePos.x - scrubXLeft) /
                                  (scrubXRight - scrubXLeft);
                        if (t < 0.f) t = 0.f;
                        if (t > 1.f) t = 1.f;

                        std::size_t target =
                            static_cast<std::size_t>(t * static_cast<float>(history.size()));

                        rebuildToIndex(target);
                    }
                }
            }
        }

        // --- Stream next replay events based on playback clock ---
        int effectiveMsPerEvent = static_cast<int>(msPerEventBase / ui.speedMultiplier);
        if (effectiveMsPerEvent < 1) effectiveMsPerEvent = 1;

        bool shouldStep =
            !stats.eof &&
            ( (!ui.paused && playbackClock.getElapsedTime().asMilliseconds() >= effectiveMsPerEvent)
              || (ui.paused && ui.stepOnce) );

        if (shouldStep) {
            playbackClock.restart();
            ui.stepOnce = false;

            auto evOpt = reader.readNext();
            if (!evOpt) {
                stats.eof           = true;
                historyComplete     = true;
                stats.replayChecksum = book.checksum();
                stats.checksumValid = true;
            } else {
                DecodedEvent ev = *evOpt;
                history.push_back(ev);

                ++stats.eventsSeen;

                std::visit([&](auto&& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, NewOrderEvent>) {
                        book.addOrder(e.side, e.price, e.qty, e.symbol);
                        stats.ordersThisSec++;
                    }
                    else if constexpr (std::is_same_v<T, CancelOrderEvent>) {
                        book.cancelOrder(e.id);
                    }
                    else if constexpr (std::is_same_v<T, TradeEvent>) {
                        // No book mutation here; trades are implied by matching.
                    }
                }, ev);

                recordEventForDebug(ev);
                updateMidFromBook();
            }
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

        // Best bid / ask for highlighting & stats
        auto bb = book.bestBid();
        auto ba = book.bestAsk();

        // Title
        if (hasFont) {
            sf::Text title(font);
            title.setCharacterSize(24);
            title.setFillColor(sf::Color(210, 210, 230));
            title.setString("MAP HFT - OrderBook Viewer (Replay)");
            title.setPosition({20.f, 10.f});
            window.draw(title);
        }

        // Stats panel (top-right) + replay checksum + speed
        if (hasFont) {
            std::string bbStr = bb ? std::to_string(bb->raw()) : "-";
            std::string baStr = ba ? std::to_string(ba->raw()) : "-";
            std::string csStr = stats.checksumValid
                                    ? std::to_string(stats.replayChecksum)
                                    : "-";

            char buf[512];
            std::snprintf(
                buf, sizeof(buf),
                "Mode: REPLAY  %s\n"
                "Best Bid: %s   Best Ask: %s\n"
                "Mid: %.2f   Spread: %.2f\n"
                "Orders/sec: %d\n"
                "Events seen: %zu%s\n"
                "Replay checksum: %s\n"
                "Speed: %.2fx",
                ui.paused ? "(PAUSED)" : "",
                bbStr.c_str(), baStr.c_str(),
                stats.mid, stats.spread,
                stats.ordersPerSec,
                stats.eventsSeen,
                stats.eof ? " (EOF)" : "",
                csStr.c_str(),
                ui.speedMultiplier
            );

            sf::Text statsText(font);
            statsText.setCharacterSize(16);
            statsText.setFillColor(sf::Color(180, 180, 200));
            statsText.setString(buf);
            statsText.setPosition({w - 420.f, 20.f});
            window.draw(statsText);
        }

        // Risk panel
        if (hasFont) {
            const auto* st = risk.findPosition("TEST");
            if (st) {
                auto posRaw   = st->netPosition.raw();
                auto posAbs   = (posRaw < 0 ? -posRaw : posRaw);
                double ratio  = (cfg.maxPosition.raw() > 0)
                                  ? static_cast<double>(posAbs) /
                                        static_cast<double>(cfg.maxPosition.raw())
                                  : 0.0;
                if (ratio > 1.0) ratio = 1.0;

                sf::Color barColor;
                if (ratio < 0.5) {
                    barColor = sf::Color(0, 200, 0, 200);
                } else if (ratio < 0.8) {
                    barColor = sf::Color(230, 200, 0, 220);
                } else {
                    barColor = sf::Color(220, 60, 60, 220);
                }

                float panelX = w - 420.f;
                float panelY = 150.f;
                float panelWidth  = 380.f;
                float panelHeight = 70.f;

                sf::RectangleShape panel;
                panel.setSize({panelWidth, panelHeight});
                panel.setPosition({panelX, panelY});
                panel.setFillColor(sf::Color(15, 18, 40, 200));
                window.draw(panel);

                float barWidth = panelWidth - 40.f;
                float barX = panelX + 20.f;
                float barY = panelY + 35.f;
                sf::RectangleShape barBg;
                barBg.setSize({barWidth, 10.f});
                barBg.setPosition({barX, barY});
                barBg.setFillColor(sf::Color(40, 45, 80));
                window.draw(barBg);

                sf::RectangleShape barFill;
                barFill.setSize({barWidth * static_cast<float>(ratio), 10.f});
                barFill.setPosition({barX, barY});
                barFill.setFillColor(barColor);
                window.draw(barFill);

                sf::Text riskText(font);
                riskText.setCharacterSize(14);
                riskText.setFillColor(sf::Color(190, 190, 220));

                char riskBuf[256];
                std::snprintf(
                    riskBuf, sizeof(riskBuf),
                    "Risk: TEST  |  Position: %lld / %lld  |  Notional: %lld",
                    static_cast<long long>(st->netPosition.raw()),
                    static_cast<long long>(cfg.maxPosition.raw()),
                    static_cast<long long>(st->netNotional.raw())
                );
                riskText.setString(riskBuf);
                riskText.setPosition({panelX + 20.f, panelY + 10.f});
                window.draw(riskText);
            }
        }

        // Ladder layout
        float ladderTop   = 200.f;
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

        float midVal = stats.mid;

        // Draw a single ladder row (with best bid/ask highlight + fading far away)
        auto drawLevelRow = [&](const map::LevelInfo* bidLvl,
                                const map::LevelInfo* askLvl,
                                float y) {
            bool isBestRow = false;
            if (bidLvl && bb && bidLvl->price.raw() == bb->raw()) {
                isBestRow = true;
            }
            if (askLvl && ba && askLvl->price.raw() == ba->raw()) {
                isBestRow = true;
            }

            if (isBestRow) {
                sf::RectangleShape hl;
                hl.setSize({w - 40.f, rowHeight});
                hl.setPosition({20.f, y});
                hl.setFillColor(sf::Color(25, 28, 60, 180));
                window.draw(hl);
            }

            auto alphaForPrice = [&](Price p) -> std::uint8_t {
                double dist = std::abs(static_cast<double>(p.raw()) - midVal);
                // fade after 5 ticks; clamp between ~70 and 255
                double fade = std::max(0.0, 1.0 - dist / 10.0);
                int a = static_cast<int>(70 + fade * 185.0);
                if (a < 50) a = 50;
                if (a > 255) a = 255;
               return static_cast<std::uint8_t>(a);

            };

            // Volume bars
            if (bidLvl) {
                float ratio = static_cast<float>(toInt(bidLvl->totalQty)) / maxQty;
                float bw    = barMaxWidth * ratio;
                std::uint8_t a = alphaForPrice(bidLvl->price);

                sf::RectangleShape bar;
                bar.setSize({bw, rowHeight - 3.f});
                bar.setPosition({bidQtyX + 50.f - bw, y + 2.f});
                bar.setFillColor(sf::Color(0, 160, 0, stats.eof ? a / 2 : a));
                window.draw(bar);
            }
            if (askLvl) {
                float ratio = static_cast<float>(toInt(askLvl->totalQty)) / maxQty;
                float bw    = barMaxWidth * ratio;
                std::uint8_t a = alphaForPrice(askLvl->price);

                sf::RectangleShape bar;
                bar.setSize({bw, rowHeight - 3.f});
                bar.setPosition({askQtyX - 10.f, y + 2.f});
                bar.setFillColor(sf::Color(200, 40, 40, stats.eof ? a / 2 : a));
                window.draw(bar);
            }

            if (!hasFont) return;

            std::optional<Price> rowPrice;

            if (bidLvl) {
                sf::Text t(font);
                t.setCharacterSize(18);
                t.setFillColor(sf::Color(0, stats.eof ? 180 : 220, 0));
                t.setString("+" + std::to_string(toInt(bidLvl->totalQty)));
                t.setPosition({bidQtyX, y});
                window.draw(t);
                rowPrice = bidLvl->price;
            }

            if (askLvl) {
                sf::Text t(font);
                t.setCharacterSize(18);
                t.setFillColor(sf::Color(230, stats.eof ? 60 : 80, 80));
                t.setString("-" + std::to_string(toInt(askLvl->totalQty)));
                t.setPosition({askQtyX, y});
                window.draw(t);

                if (!rowPrice) {
                    rowPrice = askLvl->price;
                }
            }

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
            line.setPosition({20.f, ladderTop - 5.f});
            line.setFillColor(sf::Color(40, 40, 80));
            window.draw(line);
        }

        // -----------------------------------------------------------------
        // Order-size histogram (bottom-left)
        // -----------------------------------------------------------------
        if (hasFont) {
            float histX = 40.f;
            float histY = h - 180.f;
            float histWidth  = 260.f;
            float histHeight = 120.f;

            sf::RectangleShape bg;
            bg.setSize({histWidth, histHeight});
            bg.setPosition({histX, histY});
            bg.setFillColor(sf::Color(15, 18, 40, 200));
            window.draw(bg);

            int maxCount = 1;
            for (int c : sizeBuckets) maxCount = std::max(maxCount, c);

            float barAreaWidth = histWidth - 40.f;
            float barWidth     = barAreaWidth / 5.0f;
            float barBaseY     = histY + histHeight - 25.f;
            float maxBarHeight = histHeight - 45.f;

            const char* labels[5] = { "1-5", "6-10", "11-20", "21-50", "51+" };

            for (int i = 0; i < 5; ++i) {
                float ratio = static_cast<float>(sizeBuckets[i]) /
                              static_cast<float>(maxCount);
                float bh = maxBarHeight * ratio;

                sf::RectangleShape bar;
                bar.setSize({barWidth - 8.f, bh});
                bar.setPosition({histX + 20.f + i * barWidth, barBaseY - bh});
                bar.setFillColor(sf::Color(80, 140, 220, 220));
                window.draw(bar);

                sf::Text lbl(font);
                lbl.setCharacterSize(12);
                lbl.setFillColor(sf::Color(200, 200, 230));
                lbl.setString(labels[i]);
                lbl.setPosition({histX + 20.f + i * barWidth, barBaseY + 2.f});
                window.draw(lbl);
            }

            sf::Text title(font);
            title.setCharacterSize(14);
            title.setFillColor(sf::Color(190, 190, 230));
            title.setString("Order size histogram");
            title.setPosition({histX + 10.f, histY + 5.f});
            window.draw(title);
        }

        // -----------------------------------------------------------------
        // Last event debug panel (bottom-right)
        // -----------------------------------------------------------------
        if (hasFont && !dbg.lastEventStr.empty()) {
            float panelWidth  = 380.f;
            float panelHeight = 60.f;
            float panelX      = w - panelWidth - 20.f;
            float panelY      = h - 180.f;

            sf::RectangleShape bg;
            bg.setSize({panelWidth, panelHeight});
            bg.setPosition({panelX, panelY});
            bg.setFillColor(sf::Color(15, 18, 40, 200));
            window.draw(bg);

            sf::Text t(font);
            t.setCharacterSize(13);
            t.setFillColor(sf::Color(200, 200, 230));
            t.setString(dbg.lastEventStr);
            t.setPosition({panelX + 10.f, panelY + 10.f});
            window.draw(t);
        }

        // -----------------------------------------------------------------
        // Timeline scrubber (bottom center)
        // -----------------------------------------------------------------
        {
            float scrubXLeft   = 50.f;
            float scrubXRight  = w - 50.f;
            float scrubY       = h - 30.f;
            float scrubHeight  = 6.f;

            // Track
            sf::RectangleShape track;
            track.setSize({scrubXRight - scrubXLeft, scrubHeight});
            track.setPosition({scrubXLeft, scrubY});
            track.setFillColor(sf::Color(40, 45, 80));
            window.draw(track);

            // Thumb
            if (!history.empty()) {
                float progress = static_cast<float>(stats.eventsSeen) /
                                 static_cast<float>(history.size());
                if (progress < 0.f) progress = 0.f;
                if (progress > 1.f) progress = 1.f;

                float thumbX = scrubXLeft + progress * (scrubXRight - scrubXLeft);

                sf::RectangleShape thumb;
                thumb.setSize({10.f, scrubHeight + 8.f});
                thumb.setPosition({thumbX - 5.f, scrubY - 4.f});
                thumb.setFillColor(sf::Color(200, 200, 230));
                window.draw(thumb);
            }

            if (hasFont) {
                sf::Text lbl(font);
                lbl.setCharacterSize(12);
                lbl.setFillColor(sf::Color(180, 180, 210));
                lbl.setString("Timeline (click to scrub)");
                lbl.setPosition({scrubXLeft, scrubY - 18.f});
                window.draw(lbl);
            }
        }

        // EOF banner
        if (stats.eof && hasFont) {
            sf::Text eofText(font);
            eofText.setCharacterSize(22);
            eofText.setFillColor(sf::Color(230, 200, 80));
            eofText.setString("REPLAY COMPLETE (EOF)");
            eofText.setPosition({w * 0.5f - 150.f, h - 60.f});
            window.draw(eofText);
        }

        // Hover tooltip for ladder row under mouse
        if (hasFont) {
            auto mpx = sf::Mouse::getPosition(window);
            auto m   = window.mapPixelToCoords(mpx);
            float ladderBottom = ladderTop + rows * rowHeight;
            if (m.y >= ladderTop && m.y <= ladderBottom) {
                int row = static_cast<int>((m.y - ladderTop) / rowHeight);
                if (row >= 0 && row < rows) {
                    const map::LevelInfo* bidLvl = (row < bidCount) ? &bids[row] : nullptr;
                    const map::LevelInfo* askLvl = (row < askCount) ? &asks[row] : nullptr;

                    if (bidLvl || askLvl) {
                        std::string tip;
                        if (bidLvl) {
                            tip += "Bid: px=" + std::to_string(bidLvl->price.raw()) +
                                   " qty=" + std::to_string(bidLvl->totalQty.raw());
                        }
                        if (askLvl) {
                            if (!tip.empty()) tip += "   |   ";
                            tip += "Ask: px=" + std::to_string(askLvl->price.raw()) +
                                   " qty=" + std::to_string(askLvl->totalQty.raw());
                        }

                        sf::Text tt(font);
                        tt.setCharacterSize(13);
                        tt.setFillColor(sf::Color(230, 230, 250));
                        tt.setString(tip);

                        sf::RectangleShape tbg;
                        auto bounds = tt.getLocalBounds();
float pad = 6.f;
float bx  = m.x + 12.f;
float by  = m.y - 10.f;

float bw = bounds.size.x;
float bh = bounds.size.y;

tbg.setSize({bw + 2 * pad, bh + 2 * pad});
tbg.setPosition({bx, by});

                        tbg.setFillColor(sf::Color(20, 20, 40, 220));
                        window.draw(tbg);

                        tt.setPosition({bx + pad, by + pad - 2.f});
                        window.draw(tt);
                    }
                }
            }
        }

        window.display();
    }

    return 0;
}
