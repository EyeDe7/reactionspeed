#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct AttemptRecord {
    std::string timestamp;
    double ms;       // exact milliseconds (e.g., 214.35)
    int round_index; // 1..5
    bool is_average; // false = single attempt, true = 5-round average
};

// ============================================================================
// Persistent History & Analytics Manager
// ============================================================================
class HistoryManager {
public:
    explicit HistoryManager(fs::path file_path) : file_path_(std::move(file_path)) {
        load();
    }

    void load() {
        records_.clear();
        if (!fs::exists(file_path_)) return;

        std::ifstream in(file_path_);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string ts, type_str, ms_str, round_str;
            if (std::getline(ss, ts, ',') &&
                std::getline(ss, type_str, ',') &&
                std::getline(ss, ms_str, ',') &&
                std::getline(ss, round_str, ',')) {
                try {
                    AttemptRecord rec;
                    rec.timestamp = ts;
                    rec.is_average = (type_str == "AVG5");
                    rec.ms = std::stod(ms_str);
                    rec.round_index = std::stoi(round_str);
                    records_.push_back(rec);
                } catch (...) {
                    continue;
                }
            }
        }
    }

    void addRecord(double ms, int round_idx, bool is_average) {
        AttemptRecord rec;
        rec.timestamp = currentTimestamp();
        rec.ms = ms;
        rec.round_index = round_idx;
        rec.is_average = is_average;
        records_.push_back(rec);
        appendToFile(rec);
    }

    void clear() {
        records_.clear();
        std::ofstream out(file_path_, std::ios::trunc);
        out << "# timestamp,type,ms,round\n";
    }

    std::vector<AttemptRecord> getSingleAttempts() const {
        std::vector<AttemptRecord> out;
        for (const auto& r : records_) {
            if (!r.is_average) out.push_back(r);
        }
        return out;
    }

    std::vector<AttemptRecord> getSetAverages() const {
        std::vector<AttemptRecord> out;
        for (const auto& r : records_) {
            if (r.is_average) out.push_back(r);
        }
        return out;
    }

    std::vector<AttemptRecord> getTop3Singles() const {
        auto singles = getSingleAttempts();
        std::sort(singles.begin(), singles.end(), [](const AttemptRecord& a, const AttemptRecord& b) {
            return a.ms < b.ms;
        });
        if (singles.size() > 3) singles.resize(3);
        return singles;
    }

    std::vector<AttemptRecord> getTop3Averages() const {
        auto avgs = getSetAverages();
        std::sort(avgs.begin(), avgs.end(), [](const AttemptRecord& a, const AttemptRecord& b) {
            return a.ms < b.ms;
        });
        if (avgs.size() > 3) avgs.resize(3);
        return avgs;
    }

    static double distributionDensity(double ms) {
        if (ms <= 85.0) return 0.0;
        double x = ms - 80.0;
        double mu = std::log(135.0);
        double sigma = 0.34;
        double z = (std::log(x) - mu) / sigma;
        double pdf = std::exp(-0.5 * z * z) / (x * sigma);
        double mode_x = std::exp(mu - sigma * sigma);
        double mode_z = (std::log(mode_x) - mu) / sigma;
        double peak = std::exp(-0.5 * mode_z * mode_z) / (mode_x * sigma);
        return std::clamp(pdf / peak, 0.0, 1.0);
    }

    static double fasterThanPercent(double ms) {
        if (ms <= 85.0) return 99.9;
        double x = ms - 80.0;
        double mu = std::log(135.0);
        double sigma = 0.34;
        double z = (std::log(x) - mu) / (sigma * std::sqrt(2.0));
        double cdf = 0.5 * (1.0 + std::erf(z));
        return std::clamp((1.0 - cdf) * 100.0, 0.1, 99.9);
    }

    void printSummaryToTerminal() const {
        auto singles = getSingleAttempts();
        auto top3 = getTop3Singles();
        auto top3_avg = getTop3Averages();

        std::cout << "\n====================================================================\n";
        std::cout << "               REACTION SPEED TEST - PERFORMANCE REPORT             \n";
        std::cout << "====================================================================\n";

        std::cout << "\n--- TOP 3 FASTEST SINGLE REACTIONS ---\n";
        if (top3.empty()) {
            std::cout << "  (No attempts recorded yet)\n";
        } else {
            for (size_t i = 0; i < top3.size(); ++i) {
                std::cout << "  #" << (i + 1) << "  "
                          << std::fixed << std::setprecision(2) << std::setw(7) << top3[i].ms << " ms"
                          << "   (" << static_cast<int>(std::round(top3[i].ms)) << " ms, Faster than "
                          << std::setprecision(1) << fasterThanPercent(top3[i].ms) << "%)"
                          << "   [" << top3[i].timestamp << "]\n";
            }
        }

        std::cout << "\n--- TOP 3 FASTEST 5-ROUND AVERAGES ---\n";
        if (top3_avg.empty()) {
            std::cout << "  (No completed 5-round sets yet)\n";
        } else {
            for (size_t i = 0; i < top3_avg.size(); ++i) {
                std::cout << "  #" << (i + 1) << "  "
                          << std::fixed << std::setprecision(2) << std::setw(7) << top3_avg[i].ms << " ms"
                          << "   (" << static_cast<int>(std::round(top3_avg[i].ms)) << " ms, Faster than "
                          << std::setprecision(1) << fasterThanPercent(top3_avg[i].ms) << "%)"
                          << "   [" << top3_avg[i].timestamp << "]\n";
            }
        }

        std::cout << "\n--- FULL ATTEMPT HISTORY (" << singles.size() << " total) ---\n";
        if (singles.empty()) {
            std::cout << "  (Empty)\n";
        } else {
            for (size_t i = 0; i < singles.size(); ++i) {
                std::cout << "  " << std::setw(4) << (i + 1) << ". "
                          << std::fixed << std::setprecision(2) << std::setw(7) << singles[i].ms << " ms"
                          << "  (Round " << singles[i].round_index << "/5)"
                          << "  - " << singles[i].timestamp << "\n";
            }

            double sum = 0.0;
            for (const auto& s : singles) sum += s.ms;
            double mean = sum / singles.size();
            double best = top3.front().ms;

            std::cout << "\n--- VISUAL COMPARISON CHART (Last Up To 15 Attempts vs Best & Mean) ---\n";
            size_t start_idx = singles.size() > 15 ? singles.size() - 15 : 0;
            double max_ms = 350.0;
            for (size_t i = start_idx; i < singles.size(); ++i) {
                max_ms = std::max(max_ms, singles[i].ms);
            }
            for (size_t i = start_idx; i < singles.size(); ++i) {
                int bar_len = static_cast<int>(std::round((singles[i].ms / max_ms) * 38.0));
                bar_len = std::clamp(bar_len, 1, 38);
                std::cout << "  #" << std::setw(3) << (i + 1) << " | ";
                for (int b = 0; b < bar_len; ++b) std::cout << "#";
                std::cout << " " << std::fixed << std::setprecision(1) << singles[i].ms << " ms";
                if (std::abs(singles[i].ms - best) < 1e-4) std::cout << "  <-- #1 BEST";
                std::cout << "\n";
            }
            std::cout << "\n  Overall Mean: " << std::fixed << std::setprecision(2) << mean
                      << " ms (Faster than " << std::setprecision(1) << fasterThanPercent(mean)
                      << "% | Global Median: ~215 ms)\n";
        }
        std::cout << "  History file: " << file_path_.string() << "\n";
        std::cout << "====================================================================\n\n";
    }

private:
    fs::path file_path_;
    std::vector<AttemptRecord> records_;

    static std::string currentTimestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    void appendToFile(const AttemptRecord& rec) {
        bool write_header = !fs::exists(file_path_);
        std::ofstream out(file_path_, std::ios::app);
        if (write_header) {
            out << "# timestamp,type,ms,round\n";
        }
        out << rec.timestamp << ","
            << (rec.is_average ? "AVG5" : "SINGLE") << ","
            << std::fixed << std::setprecision(3) << rec.ms << ","
            << rec.round_index << "\n";
    }
};
