#pragma once

#include "embedded_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// High-Resolution Anti-Aliased Software Canvas (ARGB8888)
// ============================================================================
class Canvas {
public:
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    void resize(int w, int h) {
        width = std::max(1, w);
        height = std::max(1, h);
        pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
    }

    static inline uint32_t blendPixel(uint32_t bg, uint32_t fg, uint8_t alpha) {
        if (alpha == 0) return bg;
        if (alpha == 255) return fg | 0xFF000000u;
        uint32_t inv = 255u - alpha;
        uint32_t r = (((fg >> 16) & 0xFFu) * alpha + ((bg >> 16) & 0xFFu) * inv) >> 8;
        uint32_t g = (((fg >> 8) & 0xFFu) * alpha + ((bg >> 8) & 0xFFu) * inv) >> 8;
        uint32_t b = ((fg & 0xFFu) * alpha + (bg & 0xFFu) * inv) >> 8;
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    inline void putPixelAlpha(int x, int y, uint32_t color, uint8_t alpha) {
        if (x < 0 || x >= width || y < 0 || y >= height || alpha == 0) return;
        size_t idx = static_cast<size_t>(y) * width + x;
        pixels[idx] = blendPixel(pixels[idx], color, alpha);
    }

    void fillRect(int x, int y, int w, int h, uint32_t color, uint8_t alpha = 255) {
        int x0 = std::max(0, x);
        int y0 = std::max(0, y);
        int x1 = std::min(width, x + w);
        int y1 = std::min(height, y + h);
        if (x0 >= x1 || y0 >= y1) return;

        if (alpha == 255) {
            uint32_t c = color | 0xFF000000u;
            for (int py = y0; py < y1; ++py) {
                uint32_t* row = &pixels[static_cast<size_t>(py) * width];
                std::fill(row + x0, row + x1, c);
            }
        } else {
            for (int py = y0; py < y1; ++py) {
                uint32_t* row = &pixels[static_cast<size_t>(py) * width];
                for (int px = x0; px < x1; ++px) {
                    row[px] = blendPixel(row[px], color, alpha);
                }
            }
        }
    }

    void fillVerticalGradient(int x, int y, int w, int h, uint32_t top_col, uint32_t bot_col) {
        int x0 = std::max(0, x);
        int y0 = std::max(0, y);
        int x1 = std::min(width, x + w);
        int y1 = std::min(height, y + h);
        if (x0 >= x1 || y0 >= y1) return;

        for (int py = y0; py < y1; ++py) {
            uint8_t t = static_cast<uint8_t>(((py - y) * 255) / std::max(1, h - 1));
            uint32_t c = blendPixel(top_col | 0xFF000000u, bot_col, t);
            uint32_t* row = &pixels[static_cast<size_t>(py) * width];
            std::fill(row + x0, row + x1, c);
        }
    }

    void fillRoundedRectAA(int x, int y, int w, int h, float radius, uint32_t color, uint8_t alpha = 255) {
        int x0 = std::max(0, x);
        int y0 = std::max(0, y);
        int x1 = std::min(width, x + w);
        int y1 = std::min(height, y + h);
        if (x0 >= x1 || y0 >= y1) return;

        float r = std::min(radius, std::min(w, h) * 0.5f);
        float cx0 = x + r;
        float cy0 = y + r;
        float cx1 = x + w - r;
        float cy1 = y + h - r;

        for (int py = y0; py < y1; ++py) {
            float fy = py + 0.5f;
            for (int px = x0; px < x1; ++px) {
                float fx = px + 0.5f;
                float dx = 0.0f;
                float dy = 0.0f;
                if (fx < cx0) dx = cx0 - fx;
                else if (fx > cx1) dx = fx - cx1;
                if (fy < cy0) dy = cy0 - fy;
                else if (fy > cy1) dy = fy - cy1;

                if (dx == 0.0f && dy == 0.0f) {
                    putPixelAlpha(px, py, color, alpha);
                } else {
                    float dist = std::sqrt(dx * dx + dy * dy);
                    float cov = std::clamp(r + 0.5f - dist, 0.0f, 1.0f);
                    if (cov > 0.0f) {
                        putPixelAlpha(px, py, color, static_cast<uint8_t>(alpha * cov));
                    }
                }
            }
        }
    }

    void strokeRoundedRectAA(int x, int y, int w, int h, float radius, float thickness, uint32_t color, uint8_t alpha = 255) {
        int pad = static_cast<int>(std::ceil(thickness + 2.0f));
        int x0 = std::max(0, x - pad);
        int y0 = std::max(0, y - pad);
        int x1 = std::min(width, x + w + pad);
        int y1 = std::min(height, y + h + pad);
        if (x0 >= x1 || y0 >= y1) return;

        float r = std::min(radius, std::min(w, h) * 0.5f);
        float half_t = thickness * 0.5f;

        for (int py = y0; py < y1; ++py) {
            float fy = py + 0.5f;
            for (int px = x0; px < x1; ++px) {
                float fx = px + 0.5f;
                float d_x = std::abs(fx - (x + w * 0.5f)) - (w * 0.5f - r);
                float d_y = std::abs(fy - (y + h * 0.5f)) - (h * 0.5f - r);
                float sdf = std::sqrt(std::max(d_x, 0.0f) * std::max(d_x, 0.0f) +
                                      std::max(d_y, 0.0f) * std::max(d_y, 0.0f)) +
                            std::min(std::max(d_x, d_y), 0.0f) - r;
                float dist_to_stroke = std::abs(sdf) - half_t;
                float cov = std::clamp(0.5f - dist_to_stroke, 0.0f, 1.0f);
                if (cov > 0.0f) {
                    putPixelAlpha(px, py, color, static_cast<uint8_t>(alpha * cov));
                }
            }
        }
    }

    void fillCircleAA(float cx, float cy, float radius, uint32_t color, uint8_t alpha = 255) {
        int x0 = std::max(0, static_cast<int>(std::floor(cx - radius - 1.5f)));
        int y0 = std::max(0, static_cast<int>(std::floor(cy - radius - 1.5f)));
        int x1 = std::min(width - 1, static_cast<int>(std::ceil(cx + radius + 1.5f)));
        int y1 = std::min(height - 1, static_cast<int>(std::ceil(cy + radius + 1.5f)));

        for (int py = y0; py <= y1; ++py) {
            float dy = (py + 0.5f) - cy;
            for (int px = x0; px <= x1; ++px) {
                float dx = (px + 0.5f) - cx;
                float dist = std::sqrt(dx * dx + dy * dy);
                float cov = std::clamp(radius + 0.5f - dist, 0.0f, 1.0f);
                if (cov > 0.0f) {
                    putPixelAlpha(px, py, color, static_cast<uint8_t>(alpha * cov));
                }
            }
        }
    }

    void strokeCircleAA(float cx, float cy, float radius, float thickness, uint32_t color, uint8_t alpha = 255) {
        float half_t = thickness * 0.5f;
        int x0 = std::max(0, static_cast<int>(std::floor(cx - radius - half_t - 1.5f)));
        int y0 = std::max(0, static_cast<int>(std::floor(cy - radius - half_t - 1.5f)));
        int x1 = std::min(width - 1, static_cast<int>(std::ceil(cx + radius + half_t + 1.5f)));
        int y1 = std::min(height - 1, static_cast<int>(std::ceil(cy + radius + half_t + 1.5f)));

        for (int py = y0; py <= y1; ++py) {
            float dy = (py + 0.5f) - cy;
            for (int px = x0; px <= x1; ++px) {
                float dx = (px + 0.5f) - cx;
                float dist = std::abs(std::sqrt(dx * dx + dy * dy) - radius);
                float cov = std::clamp(half_t + 0.5f - dist, 0.0f, 1.0f);
                if (cov > 0.0f) {
                    putPixelAlpha(px, py, color, static_cast<uint8_t>(alpha * cov));
                }
            }
        }
    }

    void drawLineAA(float x0, float y0, float x1, float y1, float thickness, uint32_t color, uint8_t alpha = 255) {
        float half_t = thickness * 0.5f;
        int min_x = std::max(0, static_cast<int>(std::floor(std::min(x0, x1) - half_t - 1.5f)));
        int max_x = std::min(width - 1, static_cast<int>(std::ceil(std::max(x0, x1) + half_t + 1.5f)));
        int min_y = std::max(0, static_cast<int>(std::floor(std::min(y0, y1) - half_t - 1.5f)));
        int max_y = std::min(height - 1, static_cast<int>(std::ceil(std::max(y0, y1) + half_t + 1.5f)));

        float vx = x1 - x0;
        float vy = y1 - y0;
        float len2 = vx * vx + vy * vy;

        for (int py = min_y; py <= max_y; ++py) {
            float fy = py + 0.5f;
            for (int px = min_x; px <= max_x; ++px) {
                float fx = px + 0.5f;
                float t = 0.0f;
                if (len2 > 1e-6f) {
                    t = std::clamp(((fx - x0) * vx + (fy - y0) * vy) / len2, 0.0f, 1.0f);
                }
                float proj_x = x0 + t * vx;
                float proj_y = y0 + t * vy;
                float dist = std::hypot(fx - proj_x, fy - proj_y);
                float cov = std::clamp(half_t + 0.5f - dist, 0.0f, 1.0f);
                if (cov > 0.0f) {
                    putPixelAlpha(px, py, color, static_cast<uint8_t>(alpha * cov));
                }
            }
        }
    }

    void drawDashedLineH(int x0, int x1, int y, uint32_t color, uint8_t alpha = 180, int dash = 6, int gap = 5) {
        if (y < 0 || y >= height) return;
        for (int x = x0; x <= x1; x += (dash + gap)) {
            int end = std::min(x1, x + dash);
            for (int px = x; px < end; ++px) {
                putPixelAlpha(px, y, color, alpha);
            }
        }
    }

    int textWidth(const EmbeddedFont& font, const std::string& text) const {
        int w = 0;
        for (unsigned char c : text) {
            if (c < 32 || c > 126) c = '?';
            w += font.glyphs[c - 32].advance;
        }
        return w;
    }

    void drawText(const EmbeddedFont& font, int x, int baseline_y, const std::string& text,
                  uint32_t color, uint8_t alpha = 255, bool shadow = true) {
        auto renderPass = [&](int pen_x, int base_y, uint32_t pass_color, uint8_t pass_alpha) {
            int top_y = base_y - font.ascent;
            for (unsigned char c : text) {
                if (c < 32 || c > 126) c = '?';
                const GlyphInfo& g = font.glyphs[c - 32];
                if (g.width > 0 && g.height > 0) {
                    int gx0 = pen_x + g.offset_x;
                    int gy0 = top_y + g.offset_y;
                    const uint8_t* bmp = font.bitmap + g.data_offset;
                    for (int row = 0; row < g.height; ++row) {
                        int py = gy0 + row;
                        if (py < 0 || py >= height) continue;
                        for (int col = 0; col < g.width; ++col) {
                            int px = gx0 + col;
                            if (px < 0 || px >= width) continue;
                            uint8_t cov = bmp[row * g.width + col];
                            if (cov > 0) {
                                uint8_t a = (pass_alpha == 255)
                                            ? cov
                                            : static_cast<uint8_t>((static_cast<uint32_t>(cov) * pass_alpha) >> 8);
                                putPixelAlpha(px, py, pass_color, a);
                            }
                        }
                    }
                }
                pen_x += g.advance;
            }
        };

        if (shadow) {
            int offset = (font.size_px >= 30) ? 2 : 1;
            renderPass(x + offset, baseline_y + offset, 0x071018, static_cast<uint8_t>(alpha * 0.45f));
        }
        renderPass(x, baseline_y, color, alpha);
    }

    void drawCenteredText(const EmbeddedFont& font, int center_x, int baseline_y, const std::string& text,
                          uint32_t color, uint8_t alpha = 255, bool shadow = true) {
        int w = textWidth(font, text);
        drawText(font, center_x - w / 2, baseline_y, text, color, alpha, shadow);
    }
};
