#pragma once

#include <string>
#include <vector>
#include <curl/curl.h>

namespace map::alpaca {

    struct Credentials {
        std::string keyId;
        std::string secretKey;
        std::string tradingUrl{"https://paper-api.alpaca.markets/v2"};
        std::string dataUrl{"https://data.alpaca.markets/v2"};
    };

    bool loadCredentialsFromEnv(const std::string& envFile, Credentials& out);

    struct HttpResponse {
        long        status{0};
        std::string body;
    };

    class HttpClient {
    public:
        explicit HttpClient(const Credentials& creds);
        ~HttpClient();

        HttpResponse get(const std::string& url) const;
        HttpResponse getData(const std::string& path) const;
        HttpResponse postJson(const std::string& path, const std::string& body) const;
        HttpResponse patchJson(const std::string& path, const std::string& body) const;
        HttpResponse deletePath(const std::string& path) const;

    private:
        Credentials creds_;
        CURL* handle_{nullptr};
        HttpResponse perform(const std::string& url,
                             const std::string& method,
                             const std::string& body = "") const;
        HttpResponse request(const std::string& url,
                             const std::string& method,
                             const std::string& body = "") const;
    };

    // Lightweight JSON helpers (avoid extra deps)
    double      extractNumber(const std::string& body, const std::string& key);
    std::string extractString(const std::string& body, const std::string& key);

} // namespace map::alpaca
