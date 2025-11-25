#include "map/alpaca/OptionChainProvider.hpp"

#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace map::alpaca {
namespace {

double parseNumber(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return 0.0;
    const auto& v = j.at(key);
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        try { return std::stod(v.get<std::string>()); } catch (...) { return 0.0; }
    }
    return 0.0;
}

std::optional<int> parseOptionalInt(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    const auto& v = j.at(key);
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_string()) {
        try { return std::stoi(v.get<std::string>()); } catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

bool parseOccSymbol(const std::string& sym, double& strike, std::string& expiration, bool& isCall) {
    if (sym.size() < 15) return false;
    std::size_t pos = sym.size() - 15;
    std::string dateStr = sym.substr(pos, 6); // YYMMDD
    char cp = sym[pos + 6];
    std::string strikeStr = sym.substr(pos + 7, 8);

    int yy = 0, mm = 0, dd = 0;
    try {
        yy = std::stoi(dateStr.substr(0, 2));
        mm = std::stoi(dateStr.substr(2, 2));
        dd = std::stoi(dateStr.substr(4, 2));
        strike = std::stod(strikeStr) / 1000.0;
    } catch (...) {
        return false;
    }

    int year = 2000 + yy;
    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, mm, dd);
    expiration = buf;
    isCall = (cp == 'C' || cp == 'c');
    return strike > 0.0;
}

int dayOfWeek(int y, int m, int d) {
    // Sakamoto algorithm, returns 0 = Sunday
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

int nthWeekdayOfMonth(int year, int month, int weekday, int nth) {
    int firstDow = dayOfWeek(year, month, 1);
    int day = 1 + (7 + weekday - firstDow) % 7;
    day += 7 * (nth - 1);
    return day;
}

} // namespace

OptionChainProvider::OptionChainProvider(const Credentials& creds, std::string feed)
    : client_(creds),
      feed_(std::move(feed)) {
    dataBaseUrl_ = creds.dataUrl;
    while (!dataBaseUrl_.empty() && dataBaseUrl_.back() == '/') dataBaseUrl_.pop_back();
    auto pos = dataBaseUrl_.rfind("/v2");
    if (pos != std::string::npos && pos + 3 == dataBaseUrl_.size()) {
        dataBaseUrl_ = dataBaseUrl_.substr(0, pos);
    }
}

std::time_t OptionChainProvider::timegmPortable(std::tm* tm) {
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

bool OptionChainProvider::isEasternDST(const std::tm& utcNow) {
    int year = utcNow.tm_year + 1900;
    int dstStartDay = nthWeekdayOfMonth(year, 3, 0, 2);  // second Sunday in March
    int dstEndDay   = nthWeekdayOfMonth(year, 11, 0, 1); // first Sunday in November

    std::tm start{};
    start.tm_year = year - 1900;
    start.tm_mon  = 2;
    start.tm_mday = dstStartDay;
    start.tm_hour = 7; // 2am EST -> 07:00 UTC

    std::tm end{};
    end.tm_year = year - 1900;
    end.tm_mon  = 10;
    end.tm_mday = dstEndDay;
    end.tm_hour = 6; // 2am EDT -> 06:00 UTC

    auto nowCopy = utcNow;
    std::time_t nowUtc   = timegmPortable(&nowCopy);
    std::time_t startUtc = timegmPortable(&start);
    std::time_t endUtc   = timegmPortable(&end);

    return nowUtc >= startUtc && nowUtc < endUtc;
}

int OptionChainProvider::easternUtcOffsetHours(const std::tm& utcNow) {
    return isEasternDST(utcNow) ? -4 : -5;
}

std::string OptionChainProvider::todayDateET(int offsetDays) {
    auto now     = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif

    int offsetHours = easternUtcOffsetHours(utc);
    auto etTp = std::chrono::system_clock::from_time_t(t) + std::chrono::hours(offsetHours);
    std::time_t etTime = std::chrono::system_clock::to_time_t(etTp);

    std::tm et{};
#if defined(_WIN32)
    gmtime_s(&et, &etTime);
#else
    gmtime_r(&etTime, &et);
#endif

    std::ostringstream oss;
    oss << std::put_time(&et, "%Y-%m-%d");
    return oss.str();
}

std::vector<types::OptionContract> OptionChainProvider::getActiveContracts(const std::string& underlyingSymbol) {
    std::vector<types::OptionContract> out;
    std::string pageToken;
    const std::string expiration = todayDateET();

    auto parseSnapshot = [&](const nlohmann::json& snap, const std::string& fallbackSymbol) {
        types::OptionContract oc;
        oc.symbol = snap.value("symbol", fallbackSymbol);

        const nlohmann::json* details = &snap;
        if (snap.contains("details") && snap["details"].is_object()) {
            details = &snap["details"];
            if (oc.symbol.empty()) {
                oc.symbol = snap["details"].value("symbol", fallbackSymbol);
            }
        }

        oc.strike     = parseNumber(*details, "strike_price");
        oc.expiration = details->value("expiration_date", expiration);

        std::string typeStr = details->value("option_type", details->value("type", ""));
        if (!typeStr.empty()) {
            char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(typeStr.front())));
            oc.isCall = (ch == 'c');
        } else if (!oc.symbol.empty()) {
            // Try OCC parsing fallback
            if (oc.symbol.size() >= 9) {
                char cp = oc.symbol[oc.symbol.size() - 9];
                oc.isCall = (cp == 'C' || cp == 'c');
            }
        }

        if (snap.contains("open_interest")) {
            oc.openInterest = parseOptionalInt(snap, "open_interest");
        } else if (details->contains("open_interest")) {
            oc.openInterest = parseOptionalInt(*details, "open_interest");
        }

        if (oc.strike <= 0.0 || oc.expiration.empty()) {
            double strikeSym = 0.0;
            std::string expSym;
            bool callSym = oc.isCall;
            if (parseOccSymbol(oc.symbol, strikeSym, expSym, callSym)) {
                if (oc.strike <= 0.0) oc.strike = strikeSym;
                if (oc.expiration.empty()) oc.expiration = expSym;
                oc.isCall = callSym;
            }
        }

        return oc;
    };

    do {
        std::ostringstream url;
        url << dataBaseUrl_ << "/v1beta1/options/snapshots/" << underlyingSymbol
            << "?feed=" << feed_
            << "&expiration_date=" << expiration
            << "&limit=1000";
        if (!pageToken.empty()) {
            url << "&page_token=" << pageToken;
        }

        HttpResponse resp = client_.getData(url.str());
        if (resp.status != 200) {
            std::cerr << "[OptionChainProvider] HTTP " << resp.status
                      << " while fetching chain: " << resp.body << std::endl;
            break;
        }

        try {
            auto j = nlohmann::json::parse(resp.body);
            if (j.contains("snapshots")) {
                const auto& snaps = j["snapshots"];
                if (snaps.is_array()) {
                    for (const auto& s : snaps) {
                        auto oc = parseSnapshot(s, "");
                        if (!oc.symbol.empty() && oc.strike > 0.0) {
                            out.push_back(std::move(oc));
                        }
                    }
                } else if (snaps.is_object()) {
                    for (auto it = snaps.begin(); it != snaps.end(); ++it) {
                        auto oc = parseSnapshot(it.value(), it.key());
                        if (!oc.symbol.empty() && oc.strike > 0.0) {
                            out.push_back(std::move(oc));
                        }
                    }
                }
            }
            pageToken.clear();
            if (j.contains("next_page_token") && !j["next_page_token"].is_null()) {
                try { pageToken = j["next_page_token"].get<std::string>(); }
                catch (...) { pageToken.clear(); }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[OptionChainProvider] Parse error: " << ex.what()
                      << " Body: " << resp.body << std::endl;
            break;
        }
    } while (!pageToken.empty());

    return out;
}

} // namespace map::alpaca
