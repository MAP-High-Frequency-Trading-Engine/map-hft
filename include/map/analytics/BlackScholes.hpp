#pragma once

#include <cmath>
#include <limits>
#include <algorithm>

namespace map::analytics {

inline double normal_cdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

inline double normal_pdf(double x) {
    static constexpr double INV_SQRT_2PI = 0.3989422804014327;
    return INV_SQRT_2PI * std::exp(-0.5 * x * x);
}

struct OptionGreeks {
    double price{0.0};
    double delta{0.0};
    double gamma{0.0};
    double theta{0.0};
    double vega{0.0};
};

inline OptionGreeks calculateBS(double S, double K, double T, double r, double sigma, bool isCall) {
    T = std::max(T, 1e-5);
    sigma = std::max(sigma, 0.001);

    double sqrtT = std::sqrt(T);
    double sigmaSqrtT = sigma * sqrtT;
    double logSK = std::log(S / K);
    double rT = r * T;

    double d1 = (logSK + rT + 0.5 * sigma * sigma * T) / sigmaSqrtT;
    double d2 = d1 - sigmaSqrtT;

    double nd1 = normal_cdf(d1);
    double nd2 = normal_cdf(d2);
    double n_d1 = normal_pdf(d1);
    double e_neg_rt = std::exp(-rT);

    OptionGreeks g;

    g.gamma = n_d1 / (S * sigmaSqrtT);
    g.vega  = S * sqrtT * n_d1 / 100.0;

    if (isCall) {
        g.price = S * nd1 - K * e_neg_rt * nd2;
        g.delta = nd1;
        g.theta = (-S * n_d1 * sigma / (2.0 * sqrtT) - r * K * e_neg_rt * nd2) / 365.0;
    } else {
        double nd_neg1 = normal_cdf(-d1);
        double nd_neg2 = normal_cdf(-d2);
        
        g.price = K * e_neg_rt * nd_neg2 - S * nd_neg1;
        g.delta = nd1 - 1.0; 
        g.theta = (-S * n_d1 * sigma / (2.0 * sqrtT) + r * K * e_neg_rt * nd_neg2) / 365.0;
    }

    return g;
}

inline double impliedVolNewton(double targetPrice,
                               double S,
                               double K,
                               double T,
                               double r,
                               bool isCall,
                               double initialGuess = 0.0) {
    T = std::max(T, 1e-5);
    double intrinsic = isCall ? std::max(0.0, S - K) : std::max(0.0, K - S);
    if (targetPrice <= intrinsic + 1e-6) return 0.001;

    auto priceForSigma = [&](double sig) {
        return calculateBS(S, K, T, r, sig, isCall).price;
    };

    double low = 0.001;
    double high = 5.0;
    double priceLow = priceForSigma(low);
    double priceHigh = priceForSigma(high);
    if (targetPrice <= priceLow) return low;
    if (targetPrice >= priceHigh) return high;

    double sigma = initialGuess > 0.0 ? initialGuess : 0.2;
    sigma = std::clamp(sigma, low, high);

    const int maxIter = 12;
    const double tol = 1e-5;

    bool newtonSucceeded = false;
    for (int i = 0; i < maxIter; ++i) {
        OptionGreeks g = calculateBS(S, K, T, r, sigma, isCall);
        double diff = g.price - targetPrice;
        if (std::abs(diff) < tol) {
            newtonSucceeded = true;
            break;
        }

        double rawVega = g.vega * 100.0;
        if (rawVega < 1e-6) {
            break;
        }

        double change = diff / rawVega;
        change = std::clamp(change, -0.5, 0.5);
        sigma = std::clamp(sigma - change, low, high);
    }

    if (newtonSucceeded) {
        return sigma;
    }

    double lo = low;
    double hi = high;
    for (int i = 0; i < 25; ++i) {
        double mid = 0.5 * (lo + hi);
        double priceMid = priceForSigma(mid);
        if (std::abs(priceMid - targetPrice) < tol) {
            return mid;
        }
        if (priceMid > targetPrice) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return 0.5 * (lo + hi);
}

} // namespace map::analytics
