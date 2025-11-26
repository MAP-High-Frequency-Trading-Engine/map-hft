#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

namespace map {

    inline std::uint64_t rdtsc() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
        return __rdtsc();
#elif defined(__i386__)
        std::uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<std::uint64_t>(hi) << 32) | lo;
#elif defined(__x86_64__)
        std::uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<std::uint64_t>(hi) << 32) | lo;
#else
        // Fallback: use chrono if rdtsc not available
        return 0;
#endif
    }

    class LatencyRecorder {
    public:
        explicit LatencyRecorder(const std::string& name = "")
            : name_(name) {}

        void record(std::uint64_t start, std::uint64_t end) {
            if (end > start) {
                deltas_.push_back(end - start);
            }
        }

        // Dump raw cycle counts; you can post-process to ns if you know CPU freq.
        void dumpCsv(const std::string& filename) const {
            std::ofstream out(filename);
            if (!out.is_open()) return;

            out << "sample,cycles\n";
            for (std::size_t i = 0; i < deltas_.size(); ++i) {
                out << i << "," << deltas_[i] << "\n";
            }
        }

        const std::string& name() const { return name_; }

    private:
        std::string                 name_;
        std::vector<std::uint64_t>  deltas_;
    };

} // namespace map
