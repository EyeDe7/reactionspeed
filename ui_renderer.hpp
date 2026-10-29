#pragma once

#include "canvas.hpp"
#include "history.hpp"
#include "net_client.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

enum class State {
    IDLE_START,
    WAITING_FOR_GREEN,
    CLICK_GREEN,
    TOO_SOON,
    ROUND_RESULT,
    SET_SUMMARY,
    CHART_VIEW
};

struct UIHitBoxes {
    int btn_name_x = 20, btn_name_y = 14, btn_name_w = 215, btn_name_h = 36;
    int btn_key_x = 245, btn_key_y = 14, btn_key_w = 175, btn_key_h = 36;
    int btn_friends_x = 0, btn_friends_y = 14, btn_friends_w = 215, btn_friends_h = 36;
    int btn_chart_x = 0, btn_chart_y = 14, btn_chart_w = 185, btn_chart_h = 36;
    int btn_new_set_x = 0, btn_new_set_y = 0, btn_new_set_w = 340, btn_new_set_h = 40;
    int btn_copy_key_x = 0, btn_copy_key_y = 0, btn_copy_key_w = 260, btn_copy_key_h = 38;
};

// ============================================================================
// UI Renderer (Icons, Progress Pills, Charts, Dashboard Cards & Modals)
// ============================================================================
namespace ui {

inline std::string formatMs(double ms) {
    std::ostringstream oss;
    oss << static_cast<int>(std::round(ms)) << " ms ("
        << std::fixed << std::setprecision(2) << ms << " ms)";
    return oss.str();
}

inline std::string formatMsShort(double ms) {
    std::ostringstream oss;
    oss << static_cast<int>(std::round(ms)) << " ms";
    return oss.str();
}

// Extracts top N unique users sorted by fastest 5-Round Average
inline std::vector<net::RemoteScoreEntry> extractUniqueFriendsByAvg(
    const net::LeaderboardData& lb,
    size_t max_count = 3
) {
    std::vector<net::RemoteScoreEntry> unique_users;
    auto mergeSource = [&](const std::vector<net::RemoteScoreEntry>& src, bool is_single_table) {
        for (const auto& entry : src) {
            if (entry.nickname.empty()) continue;
            double avg_val = is_single_table ? (entry.avg_ms > 0.0 ? entry.avg_ms : entry.ms)
                                             : (entry.ms > 0.0 ? entry.ms : entry.avg_ms);
            double best_val = entry.best_ms > 0.0 ? entry.best_ms : entry.ms;
            if (avg_val <= 0.0) continue;

            bool found = false;
            for (auto& u : unique_users) {
                if (u.nickname == entry.nickname) {
                    if (avg_val < u.ms) {
                        u.ms = avg_val;
                        u.avg_ms = avg_val;
                        u.signature_valid = entry.signature_valid;
                    }
                    if (best_val > 0.0 && (u.best_ms <= 0.0 || best_val < u.best_ms)) {
                        u.best_ms = best_val;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                net::RemoteScoreEntry item = entry;
                item.ms = avg_val;
                item.avg_ms = avg_val;
                item.best_ms = best_val;
                unique_users.push_back(std::move(item));
            }
        }
    };

    mergeSource(lb.friend_bests, false);
    mergeSource(lb.top_averages, false);
    mergeSource(lb.top_singles, true);

    std::sort(unique_users.begin(), unique_users.end(),
              [](const net::RemoteScoreEntry& a, const net::RemoteScoreEntry& b) {
                  return a.ms < b.ms;
              });
    if (unique_users.size() > max_count) {
        unique_users.resize(max_count);
    }
    return unique_users;
}

// Extracts top N unique users sorted by fastest Single Reaction
inline std::vector<net::RemoteScoreEntry> extractUniqueFriendsBySingle(
    const net::LeaderboardData& lb,
    size_t max_count = 3
) {
    std::vector<net::RemoteScoreEntry> unique_users;
    auto mergeSource = [&](const std::vector<net::RemoteScoreEntry>& src, bool is_single_table) {
        for (const auto& entry : src) {
            if (entry.nickname.empty()) continue;
            double best_val = is_single_table ? entry.ms : (entry.best_ms > 0.0 ? entry.best_ms : entry.ms);
            double avg_val = is_single_table ? (entry.avg_ms > 0.0 ? entry.avg_ms : entry.ms) : entry.ms;
            if (best_val <= 0.0) continue;

            bool found = false;
            for (auto& u : unique_users) {
                if (u.nickname == entry.nickname) {
                    if (best_val < u.best_ms) {
                        u.best_ms = best_val;
                        u.ms = best_val;
                        u.signature_valid = entry.signature_valid;
                    }
                    if (avg_val > 0.0 && (u.avg_ms <= 0.0 || avg_val < u.avg_ms)) {
                        u.avg_ms = avg_val;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                net::RemoteScoreEntry item = entry;
                item.ms = best_val;
                item.best_ms = best_val;
                item.avg_ms = avg_val;
                unique_users.push_back(std::move(item));
            }
        }
    };

    mergeSource(lb.friend_bests, false);
    mergeSource(lb.top_singles, true);
    mergeSource(lb.top_averages, false);

    std::sort(unique_users.begin(), unique_users.end(),
              [](const net::RemoteScoreEntry& a, const net::RemoteScoreEntry& b) {
                  return a.best_ms < b.best_ms;
              });
    if (unique_users.size() > max_count) {
        unique_users.resize(max_count);
    }
    return unique_users;
}

inline void drawRightText(Canvas& canvas, const EmbeddedFont& font, int right_x, int baseline_y,
                          const std::string& text, uint32_t color, uint8_t alpha = 255, bool shadow = false) {
    int w = canvas.textWidth(font, text);
    canvas.drawText(font, right_x - w, baseline_y, text, color, alpha, shadow);
}

inline void drawBadgePill(Canvas& canvas, int center_x, int top_y, const std::string& text,
                          uint32_t border_col, uint32_t text_col, int min_x, int max_x,
                          uint32_t bg_col = 0x0A1522, uint8_t bg_alpha = 242) {
    int tw = canvas.textWidth(FONT_SM, text);
    int pw = tw + 14;
    int ph = 19;
    int px = std::clamp(center_x - pw / 2, min_x, std::max(min_x, max_x - pw));
    canvas.fillRoundedRectAA(px, top_y, pw, ph, 5.0f, bg_col, bg_alpha);
    canvas.strokeRoundedRectAA(px, top_y, pw, ph, 5.0f, 1.2f, border_col, 225);
    canvas.drawCenteredText(FONT_SM, px + pw / 2, top_y + 14, text, text_col, 255, false);
}

inline void drawLightningIcon(Canvas& canvas, float cx, float cy) {
    canvas.fillCircleAA(cx, cy, 42.0f, 0xFFFFFF, 28);
    canvas.strokeCircleAA(cx, cy, 42.0f, 2.5f, 0x56CCF2, 200);
    canvas.drawLineAA(cx + 7, cy - 23, cx - 11, cy + 2, 6.5f, 0xFFD166);
    canvas.drawLineAA(cx - 11, cy + 2, cx + 9, cy - 2, 6.5f, 0xFFD166);
    canvas.drawLineAA(cx + 9, cy - 2, cx - 7, cy + 23, 6.5f, 0xFFD166);
}

inline void drawDotsIcon(Canvas& canvas, float cx, float cy) {
    for (int i = -1; i <= 1; ++i) {
        canvas.fillCircleAA(cx + i * 36.0f, cy, 11.0f, 0xFFFFFF, 245);
    }
}

inline void drawExclamationIcon(Canvas& canvas, float cx, float cy) {
    canvas.fillCircleAA(cx, cy, 38.0f, 0xFFFFFF, 30);
    canvas.strokeCircleAA(cx, cy, 38.0f, 2.5f, 0xFFD166, 220);
    canvas.drawLineAA(cx, cy - 18.0f, cx, cy + 3.0f, 7.5f, 0xFFD166);
    canvas.fillCircleAA(cx, cy + 17.0f, 4.5f, 0xFFD166);
}

inline void drawClockIcon(Canvas& canvas, float cx, float cy) {
    canvas.fillCircleAA(cx, cy, 34.0f, 0xFFFFFF, 25);
    canvas.strokeCircleAA(cx, cy, 32.0f, 3.8f, 0x38EF7D, 245);
    canvas.drawLineAA(cx, cy, cx, cy - 16.0f, 3.8f, 0xFFFFFF);
    canvas.drawLineAA(cx, cy, cx + 13.0f, cy + 8.0f, 3.8f, 0xFFFFFF);
}

inline void drawRoundProgressPills(Canvas& canvas, int cx, int y, const std::vector<double>& current_set) {
    int pill_w = 66;
    int pill_h = 24;
    int gap = 10;
    int total_w = 5 * pill_w + 4 * gap;
    int start_x = cx - total_w / 2;

    for (size_t i = 0; i < 5; ++i) {
        int px = start_x + static_cast<int>(i) * (pill_w + gap);
        bool done = i < current_set.size();
        uint32_t bg = done ? 0x1B4D3E : 0x13283F;
        uint32_t border = done ? 0x38EF7D : 0x335577;
        canvas.fillRoundedRectAA(px, y, pill_w, pill_h, 6.0f, bg, 230);
        canvas.strokeRoundedRectAA(px, y, pill_w, pill_h, 6.0f, 1.5f, border, 200);
        std::string txt = done
                          ? (std::to_string(static_cast<int>(std::round(current_set[i]))) + "ms")
                          : ("R" + std::to_string(i + 1));
        canvas.drawCenteredText(FONT_SM, px + pill_w / 2, y + 17, txt, done ? 0x38EF7D : 0x8DA2B5, 255, false);
    }
}

inline void drawVisualComparisonCharts(
    Canvas& canvas,
    int area_x, int area_y, int area_w, int area_h,
    const HistoryManager& history,
    const net::LeaderboardData& lb,
    double last_reaction_ms,
    double last_set_avg_ms
) {
    int gap = 16;
    int card_w = (area_w - gap) / 2;
    int left_x = area_x;
    int right_x = area_x + card_w + gap;

    auto singles = history.getSingleAttempts();
    auto top3 = history.getTop3Singles();
    auto top3_avg = history.getTop3Averages();
    auto unique_friends_avg = extractUniqueFriendsByAvg(lb, 3);
    auto unique_friends_single = extractUniqueFriendsBySingle(lb, 3);

    // ========================================================================
    // CARD 1: Global Bell Curve & Non-Overlapping Callout Badges
    // ========================================================================
    canvas.fillRoundedRectAA(left_x, area_y, card_w, area_h, 12.0f, 0x0C1928, 246);
    canvas.strokeRoundedRectAA(left_x, area_y, card_w, area_h, 12.0f, 1.5f, 0x294B6E, 210);

    canvas.drawText(FONT_MD, left_x + 16, area_y + 26, "GLOBAL & FRIENDS DISTRIBUTION", 0xFFD166);

    double highlight_ms = last_set_avg_ms > 0.0
                          ? last_set_avg_ms
                          : (last_reaction_ms > 0.0
                             ? last_reaction_ms
                             : (!top3_avg.empty() ? top3_avg[0].ms : (!singles.empty() ? singles.back().ms : 0.0)));

    if (highlight_ms > 0.0) {
        double pct = HistoryManager::fasterThanPercent(highlight_ms);
        std::ostringstream sub;
        sub << "You: " << static_cast<int>(std::round(highlight_ms))
            << " ms (Faster than " << std::fixed << std::setprecision(1) << pct << "% globally)";
        if (!top3.empty()) {
            sub << "   |   Your #1 Single: " << static_cast<int>(std::round(top3[0].ms)) << " ms";
        }
        canvas.drawText(FONT_SM, left_x + 16, area_y + 46, sub.str(), 0x90E0EF, 255, false);
    } else {
        canvas.drawText(FONT_SM, left_x + 16, area_y + 46,
                        "Complete rounds to compare your speed against friends & global curve", 0x9BB1C7, 255, false);
    }

    int plot1_x = left_x + 20;
    int plot1_y = area_y + 58;
    int plot1_w = card_w - 40;
    int plot1_h = area_h - 90;

    // Recessed plot container
    canvas.fillRoundedRectAA(plot1_x, plot1_y, plot1_w, plot1_h, 8.0f, 0x07111C, 250);
    canvas.strokeRoundedRectAA(plot1_x, plot1_y, plot1_w, plot1_h, 8.0f, 1.0f, 0x1B344E, 200);

    const double min_x_ms = 50.0;
    const double max_x_ms = 400.0;

    auto msToPlot1X = [&](double ms) -> float {
        double t = std::clamp((ms - min_x_ms) / (max_x_ms - min_x_ms), 0.0, 1.0);
        return static_cast<float>(plot1_x + 10 + t * (plot1_w - 20));
    };

    int curve_base_y = plot1_y + plot1_h - 6;
    int curve_max_h = std::max(40, plot1_h - 68);

    auto msToCurveY = [&](double ms) -> float {
        double d = HistoryManager::distributionDensity(ms);
        return static_cast<float>(curve_base_y - d * curve_max_h);
    };

    // Horizontal & vertical grid lines
    for (int g = 1; g <= 3; ++g) {
        int gy = plot1_y + (plot1_h * g) / 4;
        canvas.drawDashedLineH(plot1_x + 6, plot1_x + plot1_w - 6, gy, 0x1B334B, 130, 4, 4);
    }
    for (int ms_tick = 50; ms_tick <= 400; ms_tick += 50) {
        int tx = static_cast<int>(std::round(msToPlot1X(ms_tick)));
        canvas.drawLineAA(static_cast<float>(tx), static_cast<float>(plot1_y + 6),
                          static_cast<float>(tx), static_cast<float>(curve_base_y), 1.0f, 0x162B40, 110);
        canvas.drawCenteredText(FONT_SM, tx, plot1_y + plot1_h + 17,
                                std::to_string(ms_tick) + "ms", 0x9BB1C7, 235, false);
    }

    // Draw smooth global log-normal distribution curve
    float prev_cx = msToPlot1X(min_x_ms);
    float prev_cy = msToCurveY(min_x_ms);
    int inner_w = plot1_w - 20;
    for (int px = 0; px <= inner_w; ++px) {
        double ms = min_x_ms + (static_cast<double>(px) / inner_w) * (max_x_ms - min_x_ms);
        float cx = static_cast<float>(plot1_x + 10 + px);
        float cy = msToCurveY(ms);

        int top_fill = static_cast<int>(std::round(cy));
        for (int fy = top_fill; fy < curve_base_y; ++fy) {
            float ratio = 1.0f - static_cast<float>(fy - top_fill) / std::max(1, curve_base_y - top_fill);
            uint8_t a = static_cast<uint8_t>(18 + 58 * ratio);
            canvas.putPixelAlpha(static_cast<int>(cx), fy, 0x2B8CBE, a);
        }
        if (px > 0) {
            canvas.drawLineAA(prev_cx, prev_cy, cx, cy, 2.5f, 0x56CCF2);
        }
        prev_cx = cx;
        prev_cy = cy;
    }

    // Collect vertical markers and place their callout badges in non-overlapping tiers
    struct DistMarker {
        double ms;
        std::string label;
        uint32_t line_col;
        uint32_t text_col;
        float thickness;
        int priority;
    };
    std::vector<DistMarker> markers;

    markers.push_back({215.0, "Median 215ms", 0x7C93A8, 0xBFD2E3, 1.5f, 3});

    for (size_t fi = 0; fi < unique_friends_avg.size() && fi < 3; ++fi) {
        const auto& fr = unique_friends_avg[fi];
        std::string lbl = "#" + std::to_string(fi + 1) + " " + fr.nickname + " " + formatMsShort(fr.ms);
        markers.push_back({fr.ms, lbl, 0xFF758F, 0xFFB3C1, 1.8f, 2});
    }

    if (!top3.empty()) {
        markers.push_back({top3[0].ms, "Your #1: " + formatMsShort(top3[0].ms), 0xFFD166, 0xFFD166, 2.3f, 1});
    }
    if (highlight_ms > 0.0) {
        markers.push_back({highlight_ms, "You: " + formatMsShort(highlight_ms), 0x38EF7D, 0x38EF7D, 2.6f, 0});
    }

    // First draw vertical indicator lines & curve intersection dots (back-to-front by priority)
    for (const auto& m : markers) {
        float fx = msToPlot1X(m.ms);
        float cy = msToCurveY(m.ms);
        canvas.drawLineAA(fx, static_cast<float>(plot1_y + 24), fx, static_cast<float>(curve_base_y),
                          m.thickness, m.line_col, 195);
        canvas.fillCircleAA(fx, cy, 4.5f, m.line_col);
        canvas.strokeCircleAA(fx, cy, 4.5f, 1.5f, 0x07111C);
    }

    // Assign non-overlapping vertical rows (tiers) to marker badges sorted by priority
    std::vector<size_t> order(markers.size());
    for (size_t i = 0; i < markers.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (markers[a].priority != markers[b].priority) return markers[a].priority < markers[b].priority;
        return markers[a].ms < markers[b].ms;
    });

    struct PlacedSpan { int x0, x1; };
    const int num_tiers = 4;
    std::vector<PlacedSpan> tier_spans[num_tiers];

    for (size_t idx : order) {
        const auto& m = markers[idx];
        int fx = static_cast<int>(std::round(msToPlot1X(m.ms)));
        int pw = canvas.textWidth(FONT_SM, m.label) + 14;
        int px0 = std::clamp(fx - pw / 2, plot1_x + 6, std::max(plot1_x + 6, plot1_x + plot1_w - 6 - pw));
        int px1 = px0 + pw;

        int chosen_tier = 0;
        for (int t = 0; t < num_tiers; ++t) {
            bool overlap = false;
            for (const auto& sp : tier_spans[t]) {
                if (!(px1 + 6 < sp.x0 || px0 - 6 > sp.x1)) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                chosen_tier = t;
                break;
            }
            chosen_tier = t;
        }
        tier_spans[chosen_tier].push_back({px0, px1});
        int badge_y = plot1_y + 8 + chosen_tier * 22;

        // Draw subtle leader line from badge bottom to marker X
        canvas.drawLineAA(static_cast<float>(px0 + pw / 2), static_cast<float>(badge_y + 19),
                          static_cast<float>(fx), static_cast<float>( std::min(curve_base_y - 4, badge_y + 28)),
                          1.2f, m.line_col, 160);
        drawBadgePill(canvas, fx, badge_y, m.label, m.line_col, m.text_col,
                      plot1_x + 6, plot1_x + plot1_w - 6);
    }

    // ========================================================================
    // CARD 2: Attempt Progression & Record Reference Lines
    // ========================================================================
    canvas.fillRoundedRectAA(right_x, area_y, card_w, area_h, 12.0f, 0x0C1928, 246);
    canvas.strokeRoundedRectAA(right_x, area_y, card_w, area_h, 12.0f, 1.5f, 0x294B6E, 210);

    canvas.drawText(FONT_MD, right_x + 16, area_y + 26, "YOUR ATTEMPT PROGRESSION (LAST 20)", 0x38EF7D);

    int plot2_x = right_x + 54;
    int plot2_y = area_y + 58;
    int plot2_w = card_w - 72;
    int plot2_h = area_h - 90;

    canvas.fillRoundedRectAA(plot2_x, plot2_y, plot2_w, plot2_h, 8.0f, 0x07111C, 250);
    canvas.strokeRoundedRectAA(plot2_x, plot2_y, plot2_w, plot2_h, 8.0f, 1.0f, 0x1B344E, 200);

    if (singles.empty()) {
        canvas.drawText(FONT_SM, right_x + 16, area_y + 46, "No attempts recorded yet", 0x9BB1C7, 255, false);
        canvas.drawCenteredText(FONT_MD, plot2_x + plot2_w / 2, plot2_y + plot2_h / 2,
                                "Complete at least 1 round to plot progression", 0x8DA2B5);
        return;
    }

    double all_sum = 0.0;
    for (const auto& s : singles) all_sum += s.ms;
    double overall_mean = all_sum / singles.size();

    {
        std::ostringstream stats_line;
        stats_line << "Total: " << singles.size()
                   << "   |   #1 Single: " << static_cast<int>(std::round(top3[0].ms)) << " ms";
        if (!top3_avg.empty()) {
            stats_line << "   |   #1 Avg: " << static_cast<int>(std::round(top3_avg[0].ms)) << " ms";
        }
        stats_line << "   |   Mean: " << static_cast<int>(std::round(overall_mean)) << " ms";
        canvas.drawText(FONT_SM, right_x + 16, area_y + 46, stats_line.str(), 0xE2EEF8, 255, false);
    }

    size_t max_points = 20;
    size_t start_idx = singles.size() > max_points ? singles.size() - max_points : 0;
    size_t count = singles.size() - start_idx;

    double raw_min = top3[0].ms;
    double raw_max = overall_mean;
    for (size_t i = start_idx; i < singles.size(); ++i) {
        raw_min = std::min(raw_min, singles[i].ms);
        raw_max = std::max(raw_max, singles[i].ms);
    }
    double min_y_ms = std::max(20.0, std::floor((raw_min - 20.0) / 20.0) * 20.0);
    double max_y_ms = std::ceil((raw_max + 25.0) / 20.0) * 20.0;
    if (max_y_ms - min_y_ms < 80.0) max_y_ms = min_y_ms + 80.0;

    int inner_top = plot2_y + 14;
    int inner_bot = plot2_y + plot2_h - 10;
    int inner_h = std::max(40, inner_bot - inner_top);

    auto msToPlot2Y = [&](double ms) -> float {
        double t = std::clamp((ms - min_y_ms) / (max_y_ms - min_y_ms), 0.0, 1.0);
        return static_cast<float>(inner_bot - t * inner_h);
    };

    // 4 clean horizontal grid divisions + right-aligned Y-axis labels with "ms"
    for (int g = 0; g <= 4; ++g) {
        double val = min_y_ms + (max_y_ms - min_y_ms) * (4 - g) / 4.0;
        int gy = inner_top + (inner_h * g) / 4;
        canvas.drawDashedLineH(plot2_x + 6, plot2_x + plot2_w - 6, gy, 0x1B334B, 135, 4, 4);
        drawRightText(canvas, FONT_SM, plot2_x - 6, gy + 5,
                      std::to_string(static_cast<int>(std::round(val))) + "ms", 0x8DA2B5, 230, false);
    }

    // Labeled horizontal reference lines for Mean and Best (#1)
    int mean_y = static_cast<int>(std::round(msToPlot2Y(overall_mean)));
    canvas.drawDashedLineH(plot2_x + 8, plot2_x + plot2_w - 8, mean_y, 0x56CCF2, 185, 6, 4);

    int best_y = static_cast<int>(std::round(msToPlot2Y(top3[0].ms)));
    canvas.drawDashedLineH(plot2_x + 8, plot2_x + plot2_w - 8, best_y, 0xFFD166, 195, 6, 4);

    auto indexToPlot2X = [&](size_t i) -> float {
        if (count <= 1) return static_cast<float>(plot2_x + plot2_w / 2);
        return static_cast<float>(plot2_x + 18 + (i * (plot2_w - 36)) / static_cast<float>(count - 1));
    };

    // Soft area fill + connecting line
    float prev_px = 0.0f;
    float prev_py = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        double ms = singles[start_idx + i].ms;
        float fx = indexToPlot2X(i);
        float fy = msToPlot2Y(ms);

        if (i > 0) {
            int x0 = static_cast<int>(std::round(prev_px));
            int x1 = static_cast<int>(std::round(fx));
            for (int px = x0; px <= x1; ++px) {
                float t = (x1 > x0) ? static_cast<float>(px - x0) / (x1 - x0) : 0.0f;
                int top_y = static_cast<int>(std::round(prev_py + t * (fy - prev_py)));
                for (int py = top_y; py < inner_bot; ++py) {
                    float ratio = 1.0f - static_cast<float>(py - top_y) / std::max(1, inner_bot - top_y);
                    uint8_t a = static_cast<uint8_t>(12 + 42 * ratio);
                    canvas.putPixelAlpha(px, py, 0x38EF7D, a);
                }
            }
            canvas.drawLineAA(prev_px, prev_py, fx, fy, 2.4f, 0x56CCF2, 235);
        }
        prev_px = fx;
        prev_py = fy;
    }

    // Draw reference line tags on right side of plot2 (avoiding vertical overlap)
    drawBadgePill(canvas, plot2_x + plot2_w - 46, std::clamp(mean_y - 10, plot2_y + 4, inner_bot - 20),
                  "Mean " + std::to_string(static_cast<int>(std::round(overall_mean))) + "ms",
                  0x56CCF2, 0x90E0EF, plot2_x + 6, plot2_x + plot2_w - 6);
    int best_tag_y = std::clamp(best_y - 10, plot2_y + 4, inner_bot - 20);
    if (std::abs(best_tag_y - (mean_y - 10)) < 20) {
        best_tag_y = std::clamp(mean_y + 12, plot2_y + 4, inner_bot - 20);
    }
    drawBadgePill(canvas, plot2_x + plot2_w - 46, best_tag_y,
                  "Best " + std::to_string(static_cast<int>(std::round(top3[0].ms))) + "ms",
                  0xFFD166, 0xFFD166, plot2_x + 6, plot2_x + plot2_w - 6);

    // Draw data nodes, X-axis attempt numbers, and non-overlapping point value labels
    int last_label_x = -1000;
    size_t step_x = (count <= 8) ? 1 : ((count <= 14) ? 2 : 3);

    for (size_t i = 0; i < count; ++i) {
        double ms = singles[start_idx + i].ms;
        float fx = indexToPlot2X(i);
        float fy = msToPlot2Y(ms);
        bool is_best = std::abs(ms - top3[0].ms) < 1e-4;
        bool is_last = (i == count - 1);
        uint32_t dot_col = is_best ? 0xFFD166 : (ms <= overall_mean ? 0x38EF7D : 0x56CCF2);

        canvas.fillCircleAA(fx, fy, is_best || is_last ? 5.5f : 4.5f, dot_col);
        canvas.strokeCircleAA(fx, fy, is_best || is_last ? 5.5f : 4.5f, 1.5f, 0x07111C);

        // X-axis attempt number tick
        if (i == 0 || is_last || (i % step_x == 0 && static_cast<int>(fx) < static_cast<int>(indexToPlot2X(count - 1)) - 26)) {
            std::string tick_lbl = "#" + std::to_string(start_idx + i + 1);
            canvas.drawCenteredText(FONT_SM, static_cast<int>(std::round(fx)), plot2_y + plot2_h + 17,
                                    tick_lbl, 0x9BB1C7, 230, false);
        }

        // Value label above/below node with horizontal collision check
        int ix = static_cast<int>(std::round(fx));
        bool should_label = is_best || is_last || (ix - last_label_x >= 34);
        if (should_label && (is_best || is_last || ix < plot2_x + plot2_w - 88)) {
            std::string val_str = std::to_string(static_cast<int>(std::round(ms)));
            int pill_y = (fy - 24.0f >= plot2_y + 4) ? static_cast<int>(std::round(fy - 23.0f))
                                                     : static_cast<int>(std::round(fy + 8.0f));
            drawBadgePill(canvas, ix, pill_y, val_str, dot_col,
                          is_best ? 0xFFD166 : 0xFFFFFF,
                          plot2_x + 4, plot2_x + plot2_w - 4);
            last_label_x = ix;
        }
    }
}

inline std::string fitTextToWidth(const Canvas& canvas, const EmbeddedFont& font,
                                   const std::string& text, int max_w) {
    if (max_w <= 20 || canvas.textWidth(font, text) <= max_w) return text;
    std::string trimmed = text;
    while (trimmed.size() > 3 && canvas.textWidth(font, trimmed + "..") > max_w) {
        trimmed.pop_back();
    }
    return trimmed + "..";
}

inline void drawBottomStatsPanel(
    Canvas& canvas,
    int width, int height,
    int panel_y, int panel_h,
    const HistoryManager& history,
    const net::ClientConfig& cfg,
    const net::LeaderboardData& lb,
    const std::string& status_msg,
    bool show_friends_panel
) {
    canvas.fillRect(0, panel_y, width, panel_h, 0x09121B);
    canvas.fillRect(0, panel_y, width, 2, 0x223E5C);

    int pad = 12;
    int gap = 10;
    int card_w = (width - pad * 2 - gap * 3) / 4;
    int card_h = panel_h - 42;
    int card_y = panel_y + 10;
    int max_txt_w = std::max(60, card_w - 22);

    int c1_x = pad;
    int c2_x = pad + (card_w + gap);
    int c3_x = pad + (card_w + gap) * 2;
    int c4_x = pad + (card_w + gap) * 3;

    int cards_x[4] = {c1_x, c2_x, c3_x, c4_x};
    for (int i = 0; i < 4; ++i) {
        canvas.fillRoundedRectAA(cards_x[i], card_y, card_w, card_h, 9.0f, 0x101E2E, 255);
        canvas.strokeRoundedRectAA(cards_x[i], card_y, card_w, card_h, 9.0f, 1.0f, 0x243F5E, 180);
    }

    int header_y = card_y + 21;
    canvas.drawText(FONT_SM, c1_x + 11, header_y,
                    fitTextToWidth(canvas, FONT_SM, "YOUR TOP 3 (SINGLE)", max_txt_w), 0xFFD166);
    canvas.drawText(FONT_SM, c2_x + 11, header_y,
                    fitTextToWidth(canvas, FONT_SM, "YOUR TOP 3 (5-ROUND AVG)", max_txt_w), 0x38EF7D);
    canvas.drawText(FONT_SM, c3_x + 11, header_y,
                    fitTextToWidth(canvas, FONT_SM, show_friends_panel ? "FRIENDS TOP 3 (SINGLE)" : "RECENT LOCAL ROUNDS", max_txt_w),
                    0xFF758F);
    canvas.drawText(FONT_SM, c4_x + 11, header_y,
                    fitTextToWidth(canvas, FONT_SM, show_friends_panel ? "FRIENDS TOP 3 (5-ROUND AVG)" : "LOCAL SESSION SUMMARY", max_txt_w),
                    0x56CCF2);

    // Card 1: Your Top 3 (Single)
    auto top3 = history.getTop3Singles();
    if (top3.empty()) {
        canvas.drawText(FONT_SM, c1_x + 11, header_y + 23,
                        fitTextToWidth(canvas, FONT_SM, "No attempts recorded yet", max_txt_w), 0x8DA2B5);
    } else {
        for (size_t i = 0; i < top3.size(); ++i) {
            std::string ts_short = (top3[i].timestamp.size() >= 16) ? top3[i].timestamp.substr(11, 5) : top3[i].timestamp;
            std::ostringstream ss;
            ss << "#" << (i + 1) << " " << static_cast<int>(std::round(top3[i].ms))
               << " ms (" << std::fixed << std::setprecision(2) << top3[i].ms << "ms) " << ts_short;
            uint32_t col = (i == 0) ? 0xFFE082 : 0xE8F1F8;
            canvas.drawText(FONT_SM, c1_x + 11, header_y + 22 + static_cast<int>(i) * 20,
                            fitTextToWidth(canvas, FONT_SM, ss.str(), max_txt_w), col);
        }
    }

    // Card 2: Your Top 3 (5-Round Avg)
    auto top3_avg = history.getTop3Averages();
    if (top3_avg.empty()) {
        canvas.drawText(FONT_SM, c2_x + 11, header_y + 23,
                        fitTextToWidth(canvas, FONT_SM, "Complete 5 rounds for average", max_txt_w), 0x8DA2B5);
    } else {
        for (size_t i = 0; i < top3_avg.size(); ++i) {
            std::string ts_short = (top3_avg[i].timestamp.size() >= 16) ? top3_avg[i].timestamp.substr(11, 5) : top3_avg[i].timestamp;
            std::ostringstream ss;
            ss << "#" << (i + 1) << " " << static_cast<int>(std::round(top3_avg[i].ms))
               << " ms (" << std::fixed << std::setprecision(2) << top3_avg[i].ms << "ms) " << ts_short;
            uint32_t col = (i == 0) ? 0x69F0AE : 0xE8F1F8;
            canvas.drawText(FONT_SM, c2_x + 11, header_y + 22 + static_cast<int>(i) * 20,
                            fitTextToWidth(canvas, FONT_SM, ss.str(), max_txt_w), col);
        }
    }

    if (show_friends_panel) {
        auto distinct_friends_single = extractUniqueFriendsBySingle(lb, 3);
        auto distinct_friends_avg = extractUniqueFriendsByAvg(lb, 3);

        // Card 3: Friends Top 3 (Single - Unique Users)
        if (!lb.connected) {
            canvas.drawText(FONT_SM, c3_x + 11, header_y + 23,
                            fitTextToWidth(canvas, FONT_SM, lb.status_text, max_txt_w), 0xFF9EBB);
            canvas.drawText(FONT_SM, c3_x + 11, header_y + 43,
                            fitTextToWidth(canvas, FONT_SM, "Press [F] to reconnect", max_txt_w), 0x8DA2B5);
        } else if (distinct_friends_single.empty()) {
            canvas.drawText(FONT_SM, c3_x + 11, header_y + 23,
                            fitTextToWidth(canvas, FONT_SM, "Finish 5 rounds to claim #1!", max_txt_w), 0x69F0AE);
        } else {
            for (size_t i = 0; i < distinct_friends_single.size(); ++i) {
                const auto& e = distinct_friends_single[i];
                std::ostringstream ss;
                ss << "#" << (i + 1) << " " << e.nickname << ": " << std::fixed << std::setprecision(1)
                   << e.best_ms << " ms (Avg " << static_cast<int>(std::round(e.avg_ms)) << "ms)";
                uint32_t col = e.signature_valid ? ((i == 0) ? 0xFF9EBB : 0xE8F1F8) : 0xFF4757;
                canvas.drawText(FONT_SM, c3_x + 11, header_y + 22 + static_cast<int>(i) * 20,
                                fitTextToWidth(canvas, FONT_SM, ss.str(), max_txt_w), col);
            }
        }

        // Card 4: Friends Top 3 (5-Round Avg - Unique Users)
        if (!lb.connected) {
            canvas.drawText(FONT_SM, c4_x + 11, header_y + 23,
                            fitTextToWidth(canvas, FONT_SM, lb.status_text, max_txt_w), 0xFF9EBB);
            canvas.drawText(FONT_SM, c4_x + 11, header_y + 43,
                            fitTextToWidth(canvas, FONT_SM, "Press [F] to reconnect", max_txt_w), 0x8DA2B5);
        } else if (distinct_friends_avg.empty()) {
            canvas.drawText(FONT_SM, c4_x + 11, header_y + 23,
                            fitTextToWidth(canvas, FONT_SM, "Finish 5 rounds to claim #1!", max_txt_w), 0x69F0AE);
        } else {
            for (size_t i = 0; i < distinct_friends_avg.size(); ++i) {
                const auto& e = distinct_friends_avg[i];
                std::ostringstream ss;
                ss << "#" << (i + 1) << " " << e.nickname << ": " << std::fixed << std::setprecision(1)
                   << e.ms << " ms (Best " << static_cast<int>(std::round(e.best_ms)) << "ms)";
                uint32_t col = e.signature_valid ? ((i == 0) ? 0xFFD166 : 0xE8F1F8) : 0xFF4757;
                canvas.drawText(FONT_SM, c4_x + 11, header_y + 22 + static_cast<int>(i) * 20,
                                fitTextToWidth(canvas, FONT_SM, ss.str(), max_txt_w), col);
            }
        }
    } else {
        auto singles = history.getSingleAttempts();
        if (singles.empty()) {
            canvas.drawText(FONT_SM, c3_x + 11, header_y + 23, "No history yet", 0x8DA2B5);
            canvas.drawText(FONT_SM, c4_x + 11, header_y + 23, "No history yet", 0x8DA2B5);
        } else {
            int count = 0;
            for (int i = static_cast<int>(singles.size()) - 1; i >= 0 && count < 3; --i, ++count) {
                std::ostringstream ss;
                ss << "#" << (i + 1) << ": " << static_cast<int>(std::round(singles[i].ms))
                   << " ms (" << std::fixed << std::setprecision(1) << singles[i].ms << "ms) [R"
                   << singles[i].round_index << "/5]";
                canvas.drawText(FONT_SM, c3_x + 11, header_y + 22 + count * 20,
                                fitTextToWidth(canvas, FONT_SM, ss.str(), max_txt_w), 0xE8F1F8);
            }
            double sum = 0.0;
            for (const auto& s : singles) sum += s.ms;
            double mean = sum / singles.size();
            canvas.drawText(FONT_SM, c4_x + 11, header_y + 22,
                            "Total Rounds: " + std::to_string(singles.size()), 0xE8F1F8);
            canvas.drawText(FONT_SM, c4_x + 11, header_y + 42,
                            "Session Mean: " + formatMsShort(mean), 0x90E0EF);
            canvas.drawText(FONT_SM, c4_x + 11, header_y + 62,
                            "Global Percentile: " + std::to_string(static_cast<int>(std::round(HistoryManager::fasterThanPercent(mean)))) + "%",
                            0x69F0AE);
        }
    }

    std::string status_line = "Player: " + cfg.nickname + "   |   " + status_msg +
                              "   |   [SPACE] Action  [F11] Fullscreen  [N] Name  [K] Key  [G] Charts  [F] Sync  [R] Reset  [Q] Quit";
    canvas.drawCenteredText(FONT_SM, width / 2, height - 9, status_line, 0x8DA2B5, 240, false);
}

inline void drawTopBarAndModals(
    Canvas& canvas,
    int width, int height,
    State state,
    const net::ClientConfig& cfg,
    const net::LeaderboardData& lb,
    bool show_friends_panel,
    bool editing_nickname,
    const std::string& nickname_buffer,
    bool showing_key_modal,
    bool key_copied_feedback,
    UIHitBoxes& boxes
) {
    boxes.btn_name_x = 16;
    boxes.btn_name_y = 14;
    uint32_t fill_n = editing_nickname ? 0xFFD166 : 0x102236;
    uint32_t txt_n = editing_nickname ? 0x09121B : 0xFFFFFF;
    canvas.fillRoundedRectAA(boxes.btn_name_x, boxes.btn_name_y, boxes.btn_name_w, boxes.btn_name_h, 8.0f, fill_n, 230);
    canvas.strokeRoundedRectAA(boxes.btn_name_x, boxes.btn_name_y, boxes.btn_name_w, boxes.btn_name_h, 8.0f, 1.5f, 0x56CCF2, 160);
    canvas.drawCenteredText(FONT_SM, boxes.btn_name_x + boxes.btn_name_w / 2, boxes.btn_name_y + 23,
                             "Player: " + cfg.nickname + " [N]", txt_n, 255, false);

    boxes.btn_key_x = boxes.btn_name_x + boxes.btn_name_w + 10;
    boxes.btn_key_y = 14;
    uint32_t fill_k = showing_key_modal ? 0xFFD166 : 0x102236;
    uint32_t txt_k = showing_key_modal ? 0x09121B : 0xFFD166;
    canvas.fillRoundedRectAA(boxes.btn_key_x, boxes.btn_key_y, boxes.btn_key_w, boxes.btn_key_h, 8.0f, fill_k, 230);
    canvas.strokeRoundedRectAA(boxes.btn_key_x, boxes.btn_key_y, boxes.btn_key_w, boxes.btn_key_h, 8.0f, 1.5f, 0xFFD166, 160);
    canvas.drawCenteredText(FONT_SM, boxes.btn_key_x + boxes.btn_key_w / 2, boxes.btn_key_y + 23,
                             "Audit Key [K]", txt_k, 255, false);

    boxes.btn_chart_x = width - boxes.btn_chart_w - 16;
    boxes.btn_chart_y = 14;
    uint32_t fill_c = (state == State::CHART_VIEW) ? 0xFFD166 : 0x102236;
    uint32_t txt_c = (state == State::CHART_VIEW) ? 0x09121B : 0xFFFFFF;
    canvas.fillRoundedRectAA(boxes.btn_chart_x, boxes.btn_chart_y, boxes.btn_chart_w, boxes.btn_chart_h, 8.0f, fill_c, 230);
    canvas.strokeRoundedRectAA(boxes.btn_chart_x, boxes.btn_chart_y, boxes.btn_chart_w, boxes.btn_chart_h, 8.0f, 1.5f, 0xFFFFFF, 140);
    std::string label_c = (state == State::CHART_VIEW) ? "Back to Test [G]" : "Visual Charts [G]";
    canvas.drawCenteredText(FONT_SM, boxes.btn_chart_x + boxes.btn_chart_w / 2, boxes.btn_chart_y + 23, label_c, txt_c, 255, false);

    boxes.btn_friends_x = boxes.btn_chart_x - boxes.btn_friends_w - 10;
    boxes.btn_friends_y = 14;
    uint32_t fill_f = lb.connected ? 0x133829 : 0x102236;
    canvas.fillRoundedRectAA(boxes.btn_friends_x, boxes.btn_friends_y, boxes.btn_friends_w, boxes.btn_friends_h, 8.0f, fill_f, 230);
    canvas.strokeRoundedRectAA(boxes.btn_friends_x, boxes.btn_friends_y, boxes.btn_friends_w, boxes.btn_friends_h, 8.0f, 1.5f, 0x38EF7D, 160);
    std::string label_f = show_friends_panel ? "Friends Board: ON [F]" : "Friends Board: OFF [F]";
    canvas.drawCenteredText(FONT_SM, boxes.btn_friends_x + boxes.btn_friends_w / 2, boxes.btn_friends_y + 23, label_f, 0xFFFFFF, 255, false);

    if (editing_nickname) {
        int mw = 460;
        int mh = 175;
        int mx = (width - mw) / 2;
        int my = (height - 160 - mh) / 2;
        canvas.fillRoundedRectAA(mx, my, mw, mh, 14.0f, 0x0B1622, 250);
        canvas.strokeRoundedRectAA(mx, my, mw, mh, 14.0f, 2.0f, 0x56CCF2, 230);
        canvas.drawCenteredText(FONT_LG, width / 2, my + 40, "Set Your Scoreboard Nickname", 0xFFD166);
        canvas.fillRoundedRectAA(mx + 40, my + 60, mw - 80, 44, 8.0f, 0x14283D, 255);
        canvas.strokeRoundedRectAA(mx + 40, my + 60, mw - 80, 44, 8.0f, 1.5f, 0x56CCF2, 220);
        canvas.drawCenteredText(FONT_LG, width / 2, my + 90, nickname_buffer + "_", 0xFFFFFF);
        canvas.drawCenteredText(FONT_SM, width / 2, my + 144, "Press [ENTER] to save & register   |   [ESC] to cancel", 0x9BB1C7, 255, false);
    }

    if (showing_key_modal) {
        int mw = 680;
        int mh = 250;
        int mx = (width - mw) / 2;
        int my = (height - 160 - mh) / 2;
        canvas.fillRoundedRectAA(mx, my, mw, mh, 14.0f, 0x0B1622, 252);
        canvas.strokeRoundedRectAA(mx, my, mw, mh, 14.0f, 2.0f, 0xFFD166, 235);

        canvas.drawCenteredText(FONT_LG, width / 2, my + 36, "Your Ed25519 Cryptographic Identity Keys", 0xFFD166);
        canvas.drawCenteredText(FONT_SM, width / 2, my + 60,
                                 "Paste your Private Key on the website (" + cfg.server_url + ") to audit & verify your signatures:",
                                 0x90E0EF, 255, false);

        canvas.drawText(FONT_SM, mx + 26, my + 92, "Public Key (Registered on Server):", 0x8DA2B5, 255, false);
        canvas.fillRoundedRectAA(mx + 24, my + 100, mw - 48, 30, 6.0f, 0x13263A, 255);
        canvas.drawCenteredText(FONT_SM, width / 2, my + 120, cfg.public_key_hex, 0xFFFFFF, 255, false);

        canvas.drawText(FONT_SM, mx + 26, my + 150, "Private Key Seed (Copied to Clipboard — Use on Website Auditor):", 0x38EF7D, 255, false);
        canvas.fillRoundedRectAA(mx + 24, my + 158, mw - 48, 30, 6.0f, 0x13263A, 255);
        canvas.drawCenteredText(FONT_SM, width / 2, my + 178, cfg.private_seed_hex, 0xFFD166, 255, false);

        boxes.btn_copy_key_x = width / 2 - boxes.btn_copy_key_w / 2;
        boxes.btn_copy_key_y = my + 198;
        canvas.fillRoundedRectAA(boxes.btn_copy_key_x, boxes.btn_copy_key_y, boxes.btn_copy_key_w, boxes.btn_copy_key_h, 8.0f, 0x1F7A4C, 255);
        canvas.strokeRoundedRectAA(boxes.btn_copy_key_x, boxes.btn_copy_key_y, boxes.btn_copy_key_w, boxes.btn_copy_key_h, 8.0f, 1.5f, 0x38EF7D, 220);
        std::string c_txt = key_copied_feedback ? "Copied to Clipboard! [ESC to Close]" : "Copy Private Key [C]";
        canvas.drawCenteredText(FONT_SM, width / 2, boxes.btn_copy_key_y + 24, c_txt, 0xFFFFFF, 255, false);
    }
}

} // namespace ui
