#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#else
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <poll.h>
#include <sys/prctl.h>
#endif

#include "canvas.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>

// ============================================================================
// Cross-Platform System Clipboard Helper
// ============================================================================
inline void copyTextToClipboard(const std::string& text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hGlob) {
        void* ptr = GlobalLock(hGlob);
        if (ptr) {
            std::memcpy(ptr, text.c_str(), text.size() + 1);
            GlobalUnlock(hGlob);
            SetClipboardData(CF_TEXT, hGlob);
        }
    }
    CloseClipboard();
#else
    auto tryPipe = [&](const char* cmd) -> bool {
        FILE* p = popen(cmd, "w");
        if (!p) return false;
        fwrite(text.data(), 1, text.size(), p);
        return pclose(p) == 0;
    };
    if (!tryPipe("xclip -selection clipboard 2>/dev/null")) {
        tryPipe("xsel --clipboard --input 2>/dev/null");
    }
#endif
}

// ============================================================================
// Low-Latency Native Windowing & VRAM Surface Blitter (Win32 & Linux X11)
// ============================================================================
struct WindowCallbacks {
    std::function<void(int, int)> on_resize;
    std::function<void(int, int)> on_mouse_click;
    std::function<void(char, bool, bool, bool, bool)> on_key_press;
    std::function<void()> on_close;
};

class PlatformWindow {
public:
    PlatformWindow() = default;
    ~PlatformWindow() { cleanup(); }

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    bool hasValidGreenSurface(int w, int h) const {
#ifdef _WIN32
        return green_hdc_ != nullptr && green_w_ == w && green_h_ == h;
#else
        return green_pixmap_ != 0 && green_pixmap_w_ == w && green_pixmap_h_ == h;
#endif
    }

#ifdef _WIN32
    bool init(int width, int height, const char* title, WindowCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
        win_w_ = width;
        win_h_ = height;

        // Enable native DPI awareness so Windows 125%/150% display scaling does not clip the window
        if (HMODULE user32 = GetModuleHandleA("user32.dll")) {
            using SetProcessDPIAwareFn = BOOL(WINAPI*)();
            auto dpi_fn = reinterpret_cast<SetProcessDPIAwareFn>(
                reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDPIAware"))
            );
            if (dpi_fn) dpi_fn();
        }

        // Elevate process & thread scheduling priority to minimize OS wakeup jitter
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        // Request 1 ms multimedia timer period + undocumented 0.5 ms (5000 * 100ns) NT timer resolution
        timeBeginPeriod(1);
        if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
            using NtSetTimerResolutionFn = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
            auto fn = reinterpret_cast<NtSetTimerResolutionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSetTimerResolution"))
            );
            if (fn) {
                ULONG actual_res = 0;
                fn(5000, TRUE, &actual_res);
            }
        }

        HINSTANCE hInst = GetModuleHandleA(nullptr);
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "ReactionSpeedTestWindowClass";
        RegisterClassExA(&wc);

        RECT work_rc{};
        if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work_rc, 0)) {
            work_rc.left = 0;
            work_rc.top = 0;
            work_rc.right = GetSystemMetrics(SM_CXSCREEN);
            work_rc.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
        int work_w = work_rc.right - work_rc.left;
        int work_h = work_rc.bottom - work_rc.top;

        RECT rect = {0, 0, width, height};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        int win_outer_w = std::min(static_cast<int>(rect.right - rect.left), std::max(800, work_w - 20));
        int win_outer_h = std::min(static_cast<int>(rect.bottom - rect.top), std::max(600, work_h - 20));
        int pos_x = work_rc.left + std::max(0, (work_w - win_outer_w) / 2);
        int pos_y = work_rc.top + std::max(0, (work_h - win_outer_h) / 2);

        hwnd_ = CreateWindowExA(
            0, wc.lpszClassName, title,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            pos_x, pos_y,
            win_outer_w, win_outer_h,
            nullptr, nullptr, hInst, this
        );
        if (!hwnd_) return false;
        win_dc_ = GetDC(hwnd_);

        RECT client_rc{};
        if (GetClientRect(hwnd_, &client_rc)) {
            int cw = client_rc.right - client_rc.left;
            int ch = client_rc.bottom - client_rc.top;
            if (cw > 0 && ch > 0 && callbacks_.on_resize) {
                callbacks_.on_resize(cw, ch);
            }
        }
        return true;
    }

    void toggleFullscreen() {
        if (!hwnd_) return;
        DWORD style = GetWindowLongA(hwnd_, GWL_STYLE);
        if (!is_fullscreen_) {
            prev_placement_.length = sizeof(WINDOWPLACEMENT);
            MONITORINFO mi{};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetWindowPlacement(hwnd_, &prev_placement_) &&
                GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) {
                SetWindowLongA(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
                SetWindowPos(
                    hwnd_, HWND_TOP,
                    mi.rcMonitor.left, mi.rcMonitor.top,
                    mi.rcMonitor.right - mi.rcMonitor.left,
                    mi.rcMonitor.bottom - mi.rcMonitor.top,
                    SWP_NOOWNERZORDER | SWP_FRAMECHANGED
                );
                is_fullscreen_ = true;
            }
        } else {
            SetWindowLongA(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(hwnd_, &prev_placement_);
            SetWindowPos(
                hwnd_, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
            );
            is_fullscreen_ = false;
        }
    }

    void uploadGreenSurface(const Canvas& canvas) {
        if (!hwnd_) return;
        HDC target_dc = win_dc_ ? win_dc_ : GetDC(hwnd_);
        if (!green_hdc_ || green_w_ != canvas.width || green_h_ != canvas.height) {
            if (green_bmp_) DeleteObject(green_bmp_);
            if (green_hdc_) DeleteDC(green_hdc_);
            green_hdc_ = CreateCompatibleDC(target_dc);
            green_bmp_ = CreateCompatibleBitmap(target_dc, canvas.width, canvas.height);
            SelectObject(green_hdc_, green_bmp_);
            green_w_ = canvas.width;
            green_h_ = canvas.height;
        }
        bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi_.bmiHeader.biWidth = canvas.width;
        bmi_.bmiHeader.biHeight = -canvas.height;
        bmi_.bmiHeader.biPlanes = 1;
        bmi_.bmiHeader.biBitCount = 32;
        bmi_.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(
            green_hdc_, 0, 0, canvas.width, canvas.height,
            0, 0, canvas.width, canvas.height,
            canvas.pixels.data(), &bmi_, DIB_RGB_COLORS, SRCCOPY
        );
        if (!win_dc_) ReleaseDC(hwnd_, target_dc);
    }

    void presentGreenSurfaceInstant(int width, int height) {
        if (!hwnd_ || !green_hdc_) return;
        is_green_state_ = true;
        HDC target_dc = win_dc_ ? win_dc_ : GetDC(hwnd_);
        BitBlt(target_dc, 0, 0, width, height, green_hdc_, 0, 0, SRCCOPY);
        if (!win_dc_) ReleaseDC(hwnd_, target_dc);
        GdiFlush();
    }

    void presentCanvas(const Canvas& canvas, bool is_click_green) {
        if (!hwnd_) return;
        last_canvas_ = &canvas;
        is_green_state_ = is_click_green;
        win_w_ = canvas.width;
        win_h_ = canvas.height;
        HDC target_dc = win_dc_ ? win_dc_ : GetDC(hwnd_);
        presentToHdc(target_dc, canvas, is_click_green);
        if (!win_dc_) ReleaseDC(hwnd_, target_dc);
    }

    void pumpEvents() {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                if (callbacks_.on_close) callbacks_.on_close();
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    void waitForGreenOrInput(int64_t remaining_us) {
        if (remaining_us > 2500) {
            DWORD wait_ms = static_cast<DWORD>(std::clamp<int64_t>((remaining_us - 2000) / 1000, 1, 50));
            MsgWaitForMultipleObjects(0, nullptr, FALSE, wait_ms, QS_ALLINPUT);
        }
    }

    bool hasPendingInput() const {
        return HIWORD(GetQueueStatus(QS_MOUSEBUTTON | QS_KEY)) != 0;
    }

    void waitIdle(int timeout_ms) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(timeout_ms), QS_ALLINPUT);
    }

    void cleanup() {
        timeEndPeriod(1);
        if (green_bmp_) { DeleteObject(green_bmp_); green_bmp_ = nullptr; }
        if (green_hdc_) { DeleteDC(green_hdc_); green_hdc_ = nullptr; }
        if (win_dc_ && hwnd_) { ReleaseDC(hwnd_, win_dc_); win_dc_ = nullptr; }
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

private:
    HWND hwnd_ = nullptr;
    HDC win_dc_ = nullptr;
    BITMAPINFO bmi_{};
    HDC green_hdc_ = nullptr;
    HBITMAP green_bmp_ = nullptr;
    int green_w_ = 0;
    int green_h_ = 0;
    int win_w_ = 0;
    int win_h_ = 0;
    bool is_fullscreen_ = false;
    bool is_green_state_ = false;
    const Canvas* last_canvas_ = nullptr;
    WINDOWPLACEMENT prev_placement_{};
    WindowCallbacks callbacks_;

    void presentToHdc(HDC hdc, const Canvas& canvas, bool is_click_green) {
        if (is_click_green && green_hdc_ && green_w_ == canvas.width && green_h_ == canvas.height) {
            BitBlt(hdc, 0, 0, canvas.width, canvas.height, green_hdc_, 0, 0, SRCCOPY);
            return;
        }
        int dst_w = canvas.width;
        int dst_h = canvas.height;
        if (hwnd_) {
            RECT rc{};
            if (GetClientRect(hwnd_, &rc) && rc.right > 0 && rc.bottom > 0) {
                dst_w = rc.right - rc.left;
                dst_h = rc.bottom - rc.top;
            }
        }
        bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi_.bmiHeader.biWidth = canvas.width;
        bmi_.bmiHeader.biHeight = -canvas.height;
        bmi_.bmiHeader.biPlanes = 1;
        bmi_.bmiHeader.biBitCount = 32;
        bmi_.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(
            hdc, 0, 0, dst_w, dst_h,
            0, 0, canvas.width, canvas.height,
            canvas.pixels.data(), &bmi_, DIB_RGB_COLORS, SRCCOPY
        );
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        PlatformWindow* win = reinterpret_cast<PlatformWindow*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        switch (msg) {
            case WM_CREATE: {
                CREATESTRUCTA* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
                PlatformWindow* created_win = reinterpret_cast<PlatformWindow*>(cs->lpCreateParams);
                if (created_win) created_win->hwnd_ = hwnd;
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created_win));
                return 0;
            }
            case WM_SIZE:
                if (win && win->callbacks_.on_resize) {
                    int w = LOWORD(lParam);
                    int h = HIWORD(lParam);
                    if (w > 0 && h > 0) win->callbacks_.on_resize(w, h);
                }
                return 0;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                if (win && win->last_canvas_) {
                    win->presentToHdc(hdc, *win->last_canvas_, win->is_green_state_);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_LBUTTONDOWN:
                if (win && win->callbacks_.on_mouse_click) {
                    int mx = static_cast<short>(LOWORD(lParam));
                    int my = static_cast<short>(HIWORD(lParam));
                    win->callbacks_.on_mouse_click(mx, my);
                }
                return 0;
            case WM_KEYDOWN:
                if (win) {
                    if (wParam == VK_F11) {
                        win->toggleFullscreen();
                        return 0;
                    }
                    if (win->callbacks_.on_key_press) {
                        bool is_enter = (wParam == VK_RETURN);
                        bool is_space = (wParam == VK_SPACE || is_enter);
                        bool is_esc = (wParam == VK_ESCAPE);
                        bool is_bs = (wParam == VK_BACK);
                        char ch = 0;
                        if (wParam >= 'A' && wParam <= 'Z') {
                            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                            ch = static_cast<char>(shift ? wParam : (wParam - 'A' + 'a'));
                        } else if (wParam >= '0' && wParam <= '9') {
                            ch = static_cast<char>(wParam);
                        } else if (wParam == VK_OEM_MINUS) {
                            ch = '-';
                        }
                        win->callbacks_.on_key_press(ch, is_space, is_esc, is_bs, is_enter);
                    }
                }
                return 0;
            case WM_DESTROY:
                if (win && win->callbacks_.on_close) win->callbacks_.on_close();
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

#else
    bool init(int width, int height, const char* title, WindowCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
        win_w_ = width;
        win_h_ = height;

        // Reduce Linux timer slack to 1 nanosecond for sub-microsecond poll() wakeups
        prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);

        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            std::cerr << "Error: Cannot open X11 display. Check DISPLAY environment variable.\n";
            return false;
        }
        screen_ = DefaultScreen(display_);
        Window root = RootWindow(display_, screen_);

        XSetWindowAttributes swa{};
        swa.background_pixel = 0x11385E;
        swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask;

        window_ = XCreateWindow(
            display_, root,
            100, 80, width, height, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWBackPixel | CWEventMask, &swa
        );

        XStoreName(display_, window_, title);
        wm_delete_window_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display_, window_, &wm_delete_window_, 1);

        // Request X11 window manager to bypass desktop compositor for direct scanout
        Atom bypass_compositor = XInternAtom(display_, "_NET_WM_BYPASS_COMPOSITOR", False);
        if (bypass_compositor != None) {
            unsigned long bypass_val = 1;
            XChangeProperty(
                display_, window_, bypass_compositor, XA_CARDINAL, 32,
                PropModeReplace, reinterpret_cast<const unsigned char*>(&bypass_val), 1
            );
        }

        gc_ = XCreateGC(display_, window_, 0, nullptr);
        XSetGraphicsExposures(display_, gc_, False);
        XMapWindow(display_, window_);
        x11_fd_ = ConnectionNumber(display_);
        return true;
    }

    void toggleFullscreen() {
        if (!display_ || !window_) return;
        is_fullscreen_ = !is_fullscreen_;
        Atom wm_state = XInternAtom(display_, "_NET_WM_STATE", False);
        Atom fullscreen = XInternAtom(display_, "_NET_WM_STATE_FULLSCREEN", False);
        XEvent xev{};
        xev.type = ClientMessage;
        xev.xclient.window = window_;
        xev.xclient.message_type = wm_state;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = is_fullscreen_ ? 1 : 0;
        xev.xclient.data.l[1] = static_cast<long>(fullscreen);
        xev.xclient.data.l[2] = 0;
        XSendEvent(display_, DefaultRootWindow(display_), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
        XFlush(display_);
    }

    void uploadGreenSurface(const Canvas& canvas) {
        if (!display_ || !window_) return;
        int depth = DefaultDepth(display_, screen_);
        if (!green_pixmap_ || green_pixmap_w_ != canvas.width || green_pixmap_h_ != canvas.height) {
            if (green_pixmap_) XFreePixmap(display_, green_pixmap_);
            green_pixmap_ = XCreatePixmap(display_, window_, canvas.width, canvas.height, depth);
            green_pixmap_w_ = canvas.width;
            green_pixmap_h_ = canvas.height;
        }

        Visual* visual = DefaultVisual(display_, screen_);
        XImage* img = XCreateImage(
            display_, visual, depth, ZPixmap, 0,
            reinterpret_cast<char*>(const_cast<uint32_t*>(canvas.pixels.data())),
            canvas.width, canvas.height, 32, canvas.width * 4
        );
        if (img) {
            XPutImage(display_, green_pixmap_, gc_, img, 0, 0, 0, 0, canvas.width, canvas.height);
            img->data = nullptr;
            XDestroyImage(img);
        }
        XFlush(display_);
    }

    void presentGreenSurfaceInstant(int width, int height) {
        if (!display_ || !window_ || !green_pixmap_) return;
        is_green_state_ = true;
        XCopyArea(display_, green_pixmap_, window_, gc_, 0, 0, width, height, 0, 0);
        XSync(display_, False);
    }

    void presentCanvas(const Canvas& canvas, bool is_click_green) {
        if (!display_ || !window_) return;
        last_canvas_ = &canvas;
        is_green_state_ = is_click_green;
        win_w_ = canvas.width;
        win_h_ = canvas.height;

        if (is_click_green && green_pixmap_ && green_pixmap_w_ == canvas.width && green_pixmap_h_ == canvas.height) {
            XCopyArea(display_, green_pixmap_, window_, gc_, 0, 0, canvas.width, canvas.height, 0, 0);
            XFlush(display_);
            return;
        }
        Visual* visual = DefaultVisual(display_, screen_);
        int depth = DefaultDepth(display_, screen_);
        XImage* img = XCreateImage(
            display_, visual, depth, ZPixmap, 0,
            reinterpret_cast<char*>(const_cast<uint32_t*>(canvas.pixels.data())),
            canvas.width, canvas.height, 32, canvas.width * 4
        );
        if (img) {
            XPutImage(display_, window_, gc_, img, 0, 0, 0, 0, canvas.width, canvas.height);
            img->data = nullptr;
            XDestroyImage(img);
        }
        XFlush(display_);
    }

    void pumpEvents() {
        if (!display_) return;
        while (XPending(display_) > 0) {
            XEvent ev;
            XNextEvent(display_, &ev);

            if (ev.type == ClientMessage) {
                if (static_cast<Atom>(ev.xclient.data.l[0]) == wm_delete_window_) {
                    if (callbacks_.on_close) callbacks_.on_close();
                    break;
                }
            } else if (ev.type == ConfigureNotify) {
                if (ev.xconfigure.width != win_w_ || ev.xconfigure.height != win_h_) {
                    win_w_ = ev.xconfigure.width;
                    win_h_ = ev.xconfigure.height;
                    if (callbacks_.on_resize) callbacks_.on_resize(win_w_, win_h_);
                }
            } else if (ev.type == Expose) {
                if (ev.xexpose.count == 0 && last_canvas_) {
                    presentCanvas(*last_canvas_, is_green_state_);
                }
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.button == Button1 && callbacks_.on_mouse_click) {
                    callbacks_.on_mouse_click(ev.xbutton.x, ev.xbutton.y);
                }
            } else if (ev.type == KeyPress) {
                char buf[8] = {0};
                KeySym sym = NoSymbol;
                int len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &sym, nullptr);
                if (sym == XK_F11) {
                    toggleFullscreen();
                    continue;
                }
                if (callbacks_.on_key_press) {
                    bool is_enter = (sym == XK_Return || sym == XK_KP_Enter);
                    bool is_space = (sym == XK_space || is_enter);
                    bool is_esc = (sym == XK_Escape);
                    bool is_bs = (sym == XK_BackSpace);
                    char ch = (len == 1) ? buf[0] : 0;
                    callbacks_.on_key_press(ch, is_space, is_esc, is_bs, is_enter);
                }
            }
        }
    }

    void waitForGreenOrInput(int64_t remaining_us) {
        struct pollfd pfd{};
        pfd.fd = x11_fd_;
        pfd.events = POLLIN;
        if (remaining_us > 1500) {
            int timeout_ms = static_cast<int>(std::clamp<int64_t>((remaining_us - 1200) / 1000, 1, 50));
            poll(&pfd, 1, timeout_ms);
        } else {
            poll(&pfd, 1, 0);
        }
    }

    bool hasPendingInput() const {
        return display_ && XPending(display_) > 0;
    }

    void waitIdle(int timeout_ms) {
        struct pollfd pfd{};
        pfd.fd = x11_fd_;
        pfd.events = POLLIN;
        poll(&pfd, 1, timeout_ms);
    }

    void cleanup() {
        if (!display_) return;
        if (green_pixmap_) { XFreePixmap(display_, green_pixmap_); green_pixmap_ = 0; }
        if (gc_) { XFreeGC(display_, gc_); gc_ = nullptr; }
        if (window_) { XDestroyWindow(display_, window_); window_ = 0; }
        XCloseDisplay(display_);
        display_ = nullptr;
    }

private:
    Display* display_ = nullptr;
    int screen_ = 0;
    Window window_ = 0;
    GC gc_ = nullptr;
    Atom wm_delete_window_ = 0;
    Pixmap green_pixmap_ = 0;
    int green_pixmap_w_ = 0;
    int green_pixmap_h_ = 0;
    int win_w_ = 0;
    int win_h_ = 0;
    int x11_fd_ = -1;
    bool is_fullscreen_ = false;
    bool is_green_state_ = false;
    const Canvas* last_canvas_ = nullptr;
    WindowCallbacks callbacks_;
#endif
};
