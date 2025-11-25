#include "map/alpaca/AlpacaClient.hpp"

#include <curl/curl.h>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

namespace map::alpaca {
namespace {

struct CurlInitGuard {
    CurlInitGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlInitGuard() { curl_global_cleanup(); }
};

static CurlInitGuard curlGuard{};

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string trim(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\r\n");
    std::size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

std::string stripQuotes(std::string v) {
    if (!v.empty() && v.front() == '"') v.erase(v.begin());
    if (!v.empty() && v.back() == '"')  v.pop_back();
    return v;
}

std::string getenvOr(const char* key, const std::string& def) {
    const char* val = std::getenv(key);
    if (val && *val) return std::string(val);
    return def;
}

} // namespace

bool loadCredentialsFromEnv(const std::string& envFile, Credentials& out) {
    // Load from existing environment first
    out.keyId     = getenvOr("ALPACA_API_KEY_ID", "");
    out.secretKey = getenvOr("ALPACA_API_SECRET_KEY", "");
    out.tradingUrl= getenvOr("ALPACA_API_BASE_URL", out.tradingUrl);
    out.dataUrl   = getenvOr("ALPACA_DATA_URL", out.dataUrl);

    if (!envFile.empty()) {
        std::ifstream in(envFile);
        if (in.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#') continue;
                if (line.rfind("export", 0) == 0) {
                    line = line.substr(std::string("export").size());
                }
                line = trim(line);
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = trim(line.substr(0, eq));
                std::string val = stripQuotes(trim(line.substr(eq + 1)));

                if (key == "ALPACA_API_KEY_ID") {
                    out.keyId = val;
                    setenv("ALPACA_API_KEY_ID", val.c_str(), 1);
                } else if (key == "ALPACA_API_SECRET_KEY") {
                    out.secretKey = val;
                    setenv("ALPACA_API_SECRET_KEY", val.c_str(), 1);
                } else if (key == "ALPACA_API_BASE_URL") {
                    out.tradingUrl = val;
                    setenv("ALPACA_API_BASE_URL", val.c_str(), 1);
                } else if (key == "ALPACA_DATA_URL") {
                    out.dataUrl = val;
                    setenv("ALPACA_DATA_URL", val.c_str(), 1);
                }
            }
        }
    }

    return !out.keyId.empty() && !out.secretKey.empty();
}

HttpClient::HttpClient(const Credentials& creds)
    : creds_(creds) {
    handle_ = curl_easy_init();
    if (handle_) {
        curl_easy_setopt(handle_, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(handle_, CURLOPT_TCP_KEEPIDLE, 30L);
        curl_easy_setopt(handle_, CURLOPT_TCP_KEEPINTVL, 15L);
        curl_easy_setopt(handle_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle_, CURLOPT_TIMEOUT_MS, 5000L);
    }
}

HttpClient::~HttpClient() {
    if (handle_) {
        curl_easy_cleanup(handle_);
        handle_ = nullptr;
    }
}

HttpResponse HttpClient::request(const std::string& url,
                                 const std::string& method,
                                 const std::string& body) const {
    return perform(url, method, body);
}

HttpResponse HttpClient::perform(const std::string& url,
                                 const std::string& method,
                                 const std::string& body) const {
    HttpResponse resp;
    CURL* curl = handle_ ? handle_ : curl_easy_init();
    if (!curl) return resp;

    std::string responseStr;
    struct curl_slist* headers = nullptr;
    std::string keyHdr = "APCA-API-KEY-ID: " + creds_.keyId;
    std::string secHdr = "APCA-API-SECRET-KEY: " + creds_.secretKey;
    headers = curl_slist_append(headers, keyHdr.c_str());
    headers = curl_slist_append(headers, secHdr.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseStr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        resp.body = "curl error: " + std::string(curl_easy_strerror(code));
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
    resp.body = responseStr;

    curl_slist_free_all(headers);
    if (!handle_) {
        curl_easy_cleanup(curl);
    } else {
        curl_easy_reset(curl);
    }
    return resp;
}

HttpResponse HttpClient::get(const std::string& path) const {
    std::string full = (path.rfind("http", 0) == 0)
                       ? path
                       : creds_.tradingUrl + path;
    return request(full, "GET");
}

HttpResponse HttpClient::getData(const std::string& path) const {
    std::string full = (path.rfind("http", 0) == 0)
                       ? path
                       : creds_.dataUrl + path;
    return request(full, "GET");
}

HttpResponse HttpClient::postJson(const std::string& path,
                                  const std::string& body) const {
    std::string full = (path.rfind("http", 0) == 0)
                       ? path
                       : creds_.tradingUrl + path;
    return request(full, "POST", body);
}

HttpResponse HttpClient::patchJson(const std::string& path,
                                   const std::string& body) const {
    std::string full = (path.rfind("http", 0) == 0)
                       ? path
                       : creds_.tradingUrl + path;
    return request(full, "PATCH", body);
}

HttpResponse HttpClient::deletePath(const std::string& path) const {
    std::string full = (path.rfind("http", 0) == 0)
                       ? path
                       : creds_.tradingUrl + path;
    return request(full, "DELETE");
}

double extractNumber(const std::string& body, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    auto pos = body.find(pattern);
    if (pos == std::string::npos) return 0.0;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return 0.0;
    ++pos;
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '"')) ++pos;

    std::size_t end = pos;
    while (end < body.size() &&
           (std::isdigit(body[end]) || body[end] == '.' || body[end] == '-' ||
            body[end] == '+' || body[end] == 'e' || body[end] == 'E')) {
        ++end;
    }
    try {
        return std::stod(body.substr(pos, end - pos));
    } catch (...) {
        return 0.0;
    }
}

std::string extractString(const std::string& body, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    auto pos = body.find(pattern);
    if (pos == std::string::npos) return {};
    pos = body.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = body.find('"', pos);
    if (pos == std::string::npos) return {};
    ++pos;
    auto end = body.find('"', pos);
    if (end == std::string::npos) return {};
    return body.substr(pos, end - pos);
}

} // namespace map::alpaca
