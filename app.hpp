#pragma once

#include "canvas.hpp"
#include "crypto.hpp"
#include "embedded_font.hpp"
#include "history.hpp"
#include "net_client.hpp"
#include "platform_window.hpp"
#include "ui_renderer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using SteadyClock = std::chrono::steady_clock;

// ============================================================================
// Application Controller, State Machine & Non-Blocking Networking Coordinator
// ============================================================================
class ReactionBenchmarkApp {
public:
    ReactionBenchmarkApp(HistoryManager& history, net::ClientConfig& cfg, net::FriendsScoreClient& net_client)
        : history_(history), cfg_(cfg), net_client_(net_client), rng_(std::random_device{}()) {
        canvas_.resize(width_, height_);
        startAsyncInitAndPrefetch();
    }

    ~ReactionBenchmarkApp() {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        window_.cleanup();
    }

    bool initPlatform() {
        WindowCallbacks callbacks{
            [this](int w, int h) { onResize(w, h); },
            [this](int mx, int my) { onMouseClick(mx, my); },
            [this](char ch, bool is_space, bool is_esc, bool is_bs, bool is_enter) {
                onKeyPress(ch, is_space, is_esc, is_bs, is_enter);
            },
            [this]() { running_ = false; }
        };

        if (!window_.init(width_, height_, "Reaction Speed Test — Verified Friends Scoreboard", std::move(callbacks))) {
            return false;
        }
        prepareGreenSurface();
        renderAndPresent();
        return true;
    }

    void run() {
        while (running_) {
            window_.pumpEvents();
            if (!running_) break;

            if (ui_dirty_.exchange(false)) {
                if (state_ != State::WAITING_FOR_GREEN && state_ != State::CLICK_GREEN) {
                    renderAndPresent();
                }
            }

            if (state_ == State::WAITING_FOR_GREEN) {
                auto now = SteadyClock::now();
                if (now >= target_green_time_) {
                    transitionToGreen();
                    continue;
                }
                auto remaining_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    target_green_time_ - now
                ).count();
                window_.waitForGreenOrInput(remaining_us);
                if (SteadyClock::now() >= target_green_time_ && !window_.hasPendingInput()) {
                    transitionToGreen();
                }
            } else {
                window_.waitIdle(50);
            }
        }
    }

    void onResize(int w, int h) {
        width_ = std::max(760, w);
        height_ = std::max(560, h);
        canvas_.resize(width_, height_);
        renderAndPresent();
    }

    void onMouseClick(int mx, int my) {
        if (showing_key_modal_) {
            if (mx >= boxes_.btn_copy_key_x && mx <= boxes_.btn_copy_key_x + boxes_.btn_copy_key_w &&
                my >= boxes_.btn_copy_key_y && my <= boxes_.btn_copy_key_y + boxes_.btn_copy_key_h) {
                copyTextToClipboard(cfg_.private_seed_hex);
                key_copied_feedback_ = true;
                renderAndPresent();
                return;
            }
            showing_key_modal_ = false;
            renderAndPresent();
            return;
        }

        if (editing_nickname_) {
            editing_nickname_ = false;
            renderAndPresent();
            return;
        }

        if (state_ != State::WAITING_FOR_GREEN && state_ != State::CLICK_GREEN) {
            if (mx >= boxes_.btn_name_x && mx <= boxes_.btn_name_x + boxes_.btn_name_w &&
                my >= boxes_.btn_name_y && my <= boxes_.btn_name_y + boxes_.btn_name_h) {
                editing_nickname_ = true;
                nickname_buffer_ = cfg_.nickname;
                renderAndPresent();
                return;
            }
            if (mx >= boxes_.btn_key_x && mx <= boxes_.btn_key_x + boxes_.btn_key_w &&
                my >= boxes_.btn_key_y && my <= boxes_.btn_key_y + boxes_.btn_key_h) {
                showing_key_modal_ = true;
                key_copied_feedback_ = false;
                copyTextToClipboard(cfg_.private_seed_hex);
                key_copied_feedback_ = true;
                renderAndPresent();
                return;
            }
            if (mx >= boxes_.btn_chart_x && mx <= boxes_.btn_chart_x + boxes_.btn_chart_w &&
                my >= boxes_.btn_chart_y && my <= boxes_.btn_chart_y + boxes_.btn_chart_h) {
                toggleChartView();
                return;
            }
            if (mx >= boxes_.btn_friends_x && mx <= boxes_.btn_friends_x + boxes_.btn_friends_w &&
                my >= boxes_.btn_friends_y && my <= boxes_.btn_friends_y + boxes_.btn_friends_h) {
                show_friends_panel_ = !show_friends_panel_;
                if (show_friends_panel_) startAsyncLeaderboardRefresh();
                renderAndPresent();
                return;
            }
        }

        if (state_ == State::SET_SUMMARY || state_ == State::CHART_VIEW) {
            if (SteadyClock::now() < input_cooldown_until_) {
                return;
            }
            if (mx >= boxes_.btn_new_set_x && mx <= boxes_.btn_new_set_x + boxes_.btn_new_set_w &&
                my >= boxes_.btn_new_set_y && my <= boxes_.btn_new_set_y + boxes_.btn_new_set_h) {
                handleTriggerInput();
            }
            return;
        }

        handleTriggerInput();
    }

    void onKeyPress(char key, bool is_space_or_enter, bool is_escape, bool is_backspace = false, bool is_enter_only = false) {
        if (showing_key_modal_) {
            if (is_escape || key == 'k' || key == 'K' || is_space_or_enter) {
                showing_key_modal_ = false;
                renderAndPresent();
            } else if (key == 'c' || key == 'C') {
                copyTextToClipboard(cfg_.private_seed_hex);
                key_copied_feedback_ = true;
                renderAndPresent();
            }
            return;
        }

        if (editing_nickname_) {
            if (is_escape) {
                editing_nickname_ = false;
            } else if (is_enter_only) {
                if (nickname_buffer_.size() >= 2) {
                    net_client_.resetRegistrationForNewName(nickname_buffer_);
                    startAsyncInitAndPrefetch();
                }
                editing_nickname_ = false;
            } else if (is_backspace) {
                if (!nickname_buffer_.empty()) nickname_buffer_.pop_back();
            } else if (key >= 32 && key <= 126 && key != '|' && key != ' ' && nickname_buffer_.size() < 20) {
                nickname_buffer_.push_back(key);
            }
            renderAndPresent();
            return;
        }

        if (is_escape || key == 'q' || key == 'Q') {
            running_ = false;
            return;
        }
        if (is_space_or_enter) {
            handleTriggerInput();
        } else if (key == 'n' || key == 'N') {
            if (state_ != State::WAITING_FOR_GREEN && state_ != State::CLICK_GREEN) {
                editing_nickname_ = true;
                nickname_buffer_ = cfg_.nickname;
                renderAndPresent();
            }
        } else if (key == 'k' || key == 'K') {
            if (state_ != State::WAITING_FOR_GREEN && state_ != State::CLICK_GREEN) {
                showing_key_modal_ = true;
                copyTextToClipboard(cfg_.private_seed_hex);
                key_copied_feedback_ = true;
                renderAndPresent();
            }
        } else if (key == 'g' || key == 'G') {
            toggleChartView();
        } else if (key == 'f' || key == 'F') {
            show_friends_panel_ = !show_friends_panel_;
            startAsyncLeaderboardRefresh();
            renderAndPresent();
        } else if (key == 'r' || key == 'R') {
            current_set_.clear();
            used_delays_ms_.clear();
            state_ = State::IDLE_START;
            startAsyncInitAndPrefetch();
            renderAndPresent();
        } else if (key == 'h' || key == 'H') {
            history_.printSummaryToTerminal();
        } else if (key == 'c' || key == 'C') {
            history_.clear();
            renderAndPresent();
        }
    }

    void toggleChartView() {
        if (state_ == State::CHART_VIEW) {
            state_ = previous_non_chart_state_;
        } else if (state_ != State::WAITING_FOR_GREEN && state_ != State::CLICK_GREEN) {
            previous_non_chart_state_ = state_;
            state_ = State::CHART_VIEW;
            if (cfg_.online_enabled) startAsyncLeaderboardRefresh();
        }
        renderAndPresent();
    }

private:
    HistoryManager& history_;
    net::ClientConfig& cfg_;
    net::FriendsScoreClient& net_client_;
    PlatformWindow window_;

    std::mutex net_mutex_;
    net::LeaderboardData leaderboard_;
    net::ChallengeTicket prefetched_challenge_;
    net::ChallengeTicket active_challenge_;
    std::string online_status_msg_ = "Connecting to server...";
    std::atomic<bool> ui_dirty_{false};
    std::atomic<bool> worker_busy_{false};
    std::thread worker_thread_;

    bool show_friends_panel_ = true;
    std::mt19937_64 rng_;
    Canvas canvas_;
    UIHitBoxes boxes_;

    int width_ = 1120;
    int height_ = 720;
    bool running_ = true;

    State state_ = State::IDLE_START;
    State previous_non_chart_state_ = State::IDLE_START;

    SteadyClock::time_point target_green_time_{};
    SteadyClock::time_point green_shown_time_{};
    SteadyClock::time_point input_cooldown_until_{};
    double last_reaction_ms_ = 0.0;
    double last_set_avg_ms_ = 0.0;
    std::vector<double> current_set_;
    std::vector<int> used_delays_ms_;
    int pending_delay_ms_ = 2000;

    bool editing_nickname_ = false;
    std::string nickname_buffer_;
    bool showing_key_modal_ = false;
    bool key_copied_feedback_ = false;

    // ------------------------------------------------------------------------
    // Asynchronous Non-Blocking Network Operations
    // ------------------------------------------------------------------------
    void startAsyncInitAndPrefetch() {
        if (!cfg_.online_enabled || worker_busy_.exchange(true)) return;
        if (worker_thread_.joinable()) worker_thread_.join();

        worker_thread_ = std::thread([this]() {
            auto lb = net_client_.fetchLeaderboard();
            auto chk = net_client_.requestChallenge();
            {
                std::lock_guard<std::mutex> lk(net_mutex_);
                leaderboard_ = std::move(lb);
                prefetched_challenge_ = std::move(chk);
                if (prefetched_challenge_.valid) {
                    online_status_msg_ = "Ready — Signed Challenge Ticket Pre-Fetched";
                } else {
                    online_status_msg_ = leaderboard_.status_text;
                }
            }
            worker_busy_ = false;
            ui_dirty_ = true;
        });
    }

    void startAsyncLeaderboardRefresh() {
        if (!cfg_.online_enabled || worker_busy_.exchange(true)) return;
        if (worker_thread_.joinable()) worker_thread_.join();

        worker_thread_ = std::thread([this]() {
            auto lb = net_client_.fetchLeaderboard();
            {
                std::lock_guard<std::mutex> lk(net_mutex_);
                leaderboard_ = std::move(lb);
                online_status_msg_ = leaderboard_.status_text;
            }
            worker_busy_ = false;
            ui_dirty_ = true;
        });
    }

    void startAsyncSubmitAndPrefetch(net::ChallengeTicket ticket,
                                     std::vector<double> rounds,
                                     std::vector<int> delays) {
        if (worker_thread_.joinable()) worker_thread_.join();
        worker_busy_ = true;

        worker_thread_ = std::thread([this, ticket = std::move(ticket), rounds = std::move(rounds), delays = std::move(delays)]() {
            std::string sub_msg;
            if (ticket.valid) {
                net_client_.submitVerifiedSet(ticket, rounds, delays, sub_msg);
            } else {
                sub_msg = "Saved locally (no active server ticket)";
            }
            auto lb = net_client_.fetchLeaderboard();
            auto next_chk = net_client_.requestChallenge();
            {
                std::lock_guard<std::mutex> lk(net_mutex_);
                online_status_msg_ = sub_msg;
                leaderboard_ = std::move(lb);
                prefetched_challenge_ = std::move(next_chk);
            }
            worker_busy_ = false;
            ui_dirty_ = true;
        });
    }

    void startWaitingForGreen() {
        if (current_set_.empty()) {
            std::lock_guard<std::mutex> lk(net_mutex_);
            if (prefetched_challenge_.valid) {
                active_challenge_ = std::move(prefetched_challenge_);
                prefetched_challenge_ = net::ChallengeTicket{};
                online_status_msg_ = "Signed Challenge Active (" + active_challenge_.challenge_id.substr(0, 10) + ")";
            } else {
                active_challenge_ = net::ChallengeTicket{};
            }
        }

        size_t round_idx = current_set_.size();
        if (active_challenge_.valid && round_idx < active_challenge_.wait_delays_ms.size()) {
            pending_delay_ms_ = active_challenge_.wait_delays_ms[round_idx];
        } else {
            std::uniform_int_distribution<int> dist_ms(1500, 4500);
            pending_delay_ms_ = dist_ms(rng_);
        }

        prepareGreenSurface();

        state_ = State::WAITING_FOR_GREEN;
        renderAndPresent();
        target_green_time_ = SteadyClock::now() + std::chrono::milliseconds(pending_delay_ms_);
    }

    void transitionToGreen() {
        state_ = State::CLICK_GREEN;
        presentGreenSurfaceInstant();
        green_shown_time_ = SteadyClock::now();
    }

    void handleTriggerInput() {
        auto click_time = SteadyClock::now();

        if (state_ != State::WAITING_FOR_GREEN && state_ != State::CLICK_GREEN) {
            if (click_time < input_cooldown_until_) {
                return;
            }
        }

        switch (state_) {
            case State::IDLE_START:
            case State::TOO_SOON:
            case State::ROUND_RESULT:
                startWaitingForGreen();
                break;

            case State::SET_SUMMARY:
            case State::CHART_VIEW:
                if (current_set_.size() >= 5) {
                    current_set_.clear();
                    used_delays_ms_.clear();
                }
                startWaitingForGreen();
                break;

            case State::WAITING_FOR_GREEN:
                state_ = State::TOO_SOON;
                input_cooldown_until_ = click_time + std::chrono::milliseconds(280);
                renderAndPresent();
                break;

            case State::CLICK_GREEN: {
                double elapsed_ms = std::chrono::duration<double, std::milli>(
                    click_time - green_shown_time_
                ).count();
                last_reaction_ms_ = elapsed_ms;
                current_set_.push_back(elapsed_ms);
                used_delays_ms_.push_back(pending_delay_ms_);

                int round_num = static_cast<int>(current_set_.size());
                history_.addRecord(elapsed_ms, round_num, false);

                if (current_set_.size() >= 5) {
                    double sum = std::accumulate(current_set_.begin(), current_set_.end(), 0.0);
                    last_set_avg_ms_ = sum / current_set_.size();
                    history_.addRecord(last_set_avg_ms_, 5, true);

                    state_ = State::SET_SUMMARY;
                    previous_non_chart_state_ = State::SET_SUMMARY;
                    input_cooldown_until_ = click_time + std::chrono::milliseconds(750);

                    {
                        std::lock_guard<std::mutex> lk(net_mutex_);
                        online_status_msg_ = "Signing & submitting 5-round set to server...";
                    }
                    renderAndPresent();

                    startAsyncSubmitAndPrefetch(active_challenge_, current_set_, used_delays_ms_);
                    active_challenge_ = net::ChallengeTicket{};
                } else {
                    state_ = State::ROUND_RESULT;
                    previous_non_chart_state_ = State::ROUND_RESULT;
                    input_cooldown_until_ = click_time + std::chrono::milliseconds(250);
                    renderAndPresent();
                }
                break;
            }
        }
    }

    void renderFrame() {
        net::LeaderboardData lb_snapshot;
        std::string status_snapshot;
        {
            std::lock_guard<std::mutex> lk(net_mutex_);
            lb_snapshot = leaderboard_;
            status_snapshot = online_status_msg_;
        }

        int panel_h = 158;
        int main_h = (state_ == State::WAITING_FOR_GREEN || state_ == State::CLICK_GREEN)
                     ? height_
                     : (height_ - panel_h);

        if (state_ == State::WAITING_FOR_GREEN) {
            canvas_.fillVerticalGradient(0, 0, width_, main_h, 0xD62839, 0xA81B29);
        } else if (state_ == State::CLICK_GREEN) {
            canvas_.fillVerticalGradient(0, 0, width_, main_h, 0x2ED573, 0x1EAE5B);
        } else if (state_ == State::CHART_VIEW || state_ == State::SET_SUMMARY) {
            canvas_.fillVerticalGradient(0, 0, width_, main_h, 0x132A42, 0x0B1826);
        } else {
            canvas_.fillVerticalGradient(0, 0, width_, main_h, 0x1D5C96, 0x11385E);
        }

        int cx = width_ / 2;
        int cy = main_h / 2;

        switch (state_) {
            case State::IDLE_START:
                ui::drawLightningIcon(canvas_, static_cast<float>(cx), static_cast<float>(cy - 114));
                canvas_.drawCenteredText(FONT_XXL, cx, cy + 8, "Reaction Speed Test", 0xFFFFFF);
                canvas_.drawCenteredText(FONT_LG, cx, cy + 52, "When the red screen turns green, click as quickly as you can.", 0xE2F1FF);
                canvas_.drawCenteredText(FONT_MD, cx, cy + 90, "Click anywhere or press SPACE to begin 5-round set", 0xFFD166);
                break;

            case State::WAITING_FOR_GREEN:
                ui::drawDotsIcon(canvas_, static_cast<float>(cx), static_cast<float>(cy - 68));
                canvas_.drawCenteredText(FONT_XXL, cx, cy + 20, "Wait for green...", 0xFFFFFF);
                break;

            case State::CLICK_GREEN:
                ui::drawDotsIcon(canvas_, static_cast<float>(cx), static_cast<float>(cy - 68));
                canvas_.drawCenteredText(FONT_XXL, cx, cy + 20, "CLICK NOW!", 0xFFFFFF);
                break;

            case State::TOO_SOON:
                ui::drawRoundProgressPills(canvas_, cx, 62, current_set_);
                ui::drawExclamationIcon(canvas_, static_cast<float>(cx), static_cast<float>(cy - 92));
                canvas_.drawCenteredText(FONT_XXL, cx, cy + 14, "Too soon!", 0xFFFFFF);
                canvas_.drawCenteredText(FONT_LG, cx, cy + 58, "Click anywhere or press SPACE to retry this round.", 0xE2F1FF);
                break;

            case State::ROUND_RESULT: {
                ui::drawRoundProgressPills(canvas_, cx, 62, current_set_);
                ui::drawClockIcon(canvas_, static_cast<float>(cx), static_cast<float>(cy - 96));
                canvas_.drawCenteredText(FONT_XXL, cx, cy + 8, ui::formatMs(last_reaction_ms_), 0xFFFFFF);
                std::ostringstream prog;
                prog << "Round " << current_set_.size() << " of 5   —   Click or press SPACE for next round";
                canvas_.drawCenteredText(FONT_LG, cx, cy + 50, prog.str(), 0xE2F1FF);

                double pct = HistoryManager::fasterThanPercent(last_reaction_ms_);
                std::ostringstream pct_ss;
                pct_ss << "Faster than " << std::fixed << std::setprecision(1) << pct
                       << "% of global reactions (Median: 215 ms)";
                canvas_.drawCenteredText(FONT_SM, cx, cy + 84, pct_ss.str(), 0xFFD166);
                break;
            }

            case State::SET_SUMMARY: {
                canvas_.drawCenteredText(FONT_MD, cx, 28, "5-ROUND SET COMPLETED", 0x38EF7D);
                canvas_.drawCenteredText(FONT_XL, cx, 62, "Average: " + ui::formatMs(last_set_avg_ms_), 0xFFFFFF);
                ui::drawRoundProgressPills(canvas_, cx, 78, current_set_);

                int chart_y = 114;
                int chart_h = main_h - chart_y - 54;
                if (chart_h >= 180) {
                    ui::drawVisualComparisonCharts(
                        canvas_, 20, chart_y, width_ - 40, chart_h,
                        history_, lb_snapshot, last_reaction_ms_, last_set_avg_ms_
                    );
                }

                boxes_.btn_new_set_x = cx - boxes_.btn_new_set_w / 2;
                boxes_.btn_new_set_y = main_h - 46;
                canvas_.fillRoundedRectAA(boxes_.btn_new_set_x, boxes_.btn_new_set_y, boxes_.btn_new_set_w, boxes_.btn_new_set_h, 9.0f, 0x1F7A4C, 245);
                canvas_.strokeRoundedRectAA(boxes_.btn_new_set_x, boxes_.btn_new_set_y, boxes_.btn_new_set_w, boxes_.btn_new_set_h, 9.0f, 1.5f, 0x38EF7D, 220);
                canvas_.drawCenteredText(FONT_MD, cx, boxes_.btn_new_set_y + 26,
                                         "Start New 5-Round Set [SPACE]", 0xFFFFFF, 255, false);
                break;
            }

            case State::CHART_VIEW: {
                canvas_.drawCenteredText(FONT_XL, cx, 42, "Performance & Friends Comparison", 0xFFFFFF);
                int chart_y = 62;
                int chart_h = main_h - chart_y - 54;
                ui::drawVisualComparisonCharts(
                    canvas_, 20, chart_y, width_ - 40, chart_h,
                    history_, lb_snapshot, last_reaction_ms_, last_set_avg_ms_
                );

                boxes_.btn_new_set_x = cx - boxes_.btn_new_set_w / 2;
                boxes_.btn_new_set_y = main_h - 46;
                canvas_.fillRoundedRectAA(boxes_.btn_new_set_x, boxes_.btn_new_set_y, boxes_.btn_new_set_w, boxes_.btn_new_set_h, 9.0f, 0x1F7A4C, 245);
                canvas_.strokeRoundedRectAA(boxes_.btn_new_set_x, boxes_.btn_new_set_y, boxes_.btn_new_set_w, boxes_.btn_new_set_h, 9.0f, 1.5f, 0x38EF7D, 220);
                canvas_.drawCenteredText(FONT_MD, cx, boxes_.btn_new_set_y + 26,
                                         "Start New 5-Round Set [SPACE]", 0xFFFFFF, 255, false);
                break;
            }
        }

        if (state_ != State::WAITING_FOR_GREEN && state_ != State::CLICK_GREEN) {
            ui::drawTopBarAndModals(
                canvas_, width_, height_, state_, cfg_, lb_snapshot,
                show_friends_panel_, editing_nickname_, nickname_buffer_,
                showing_key_modal_, key_copied_feedback_, boxes_
            );
            ui::drawBottomStatsPanel(
                canvas_, width_, height_, main_h, panel_h,
                history_, cfg_, lb_snapshot, status_snapshot, show_friends_panel_
            );
        }
    }

    void prepareGreenSurface() {
        State saved = state_;
        state_ = State::CLICK_GREEN;
        renderFrame();
        state_ = saved;
        window_.uploadGreenSurface(canvas_);
    }

    void presentGreenSurfaceInstant() {
        if (!window_.hasValidGreenSurface(width_, height_)) {
            prepareGreenSurface();
        }
        window_.presentGreenSurfaceInstant(width_, height_);
    }

    void renderAndPresent() {
        renderFrame();
        window_.presentCanvas(canvas_, state_ == State::CLICK_GREEN);
    }
};
