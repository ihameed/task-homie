/*
Copyright (c) 2014, Imran Hameed
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <windows.h>
#include <shellapi.h>

template <typename mod>
struct handle_ty final {
    using t = typename mod::t;

    t handle;

    handle_ty(const t & handle) : handle(handle) { }

    ~handle_ty() { _destroy_(); }

    handle_ty(handle_ty && x) { _move_(x); }

    handle_ty &
    operator = (handle_ty && x) { _destroy_(); _move_(x); return *this; }

    handle_ty(const handle_ty &) = delete;

    handle_ty &
    operator = (const handle_ty &) = delete;

    void
    _move_(handle_ty &x) { handle = x.handle; mod::invalidate(x.handle); }

    void
    _destroy_() { if (mod::is_valid(handle)) mod::destroy(handle); }
};

struct rgn_ty final {
    using t = HRGN;

    static bool
    is_valid(t handle) { return handle != nullptr; }

    static void
    invalidate(t &handle) { handle = nullptr; }

    static void
    destroy(t handle) { DeleteObject(handle); }
};

const WCHAR TaskbarCls [] = L"Shell_TrayWnd";

template <typename t>
static BOOL CALLBACK
enum_windows_(HWND hwnd, LPARAM env)
{ return (*reinterpret_cast<t *>(env))(hwnd); }

template <typename t>
static void
enum_windows(const t & fun)
{ EnumWindows(enum_windows_<t>, reinterpret_cast<LPARAM>(&fun)); }

template <size_t MemLen>
static bool
str_eq_p(const WCHAR (&x) [MemLen], const WCHAR *y, const size_t y_str_len) {
    const auto StrLen = MemLen - 1;
    if (StrLen != y_str_len) return false;
    for (size_t i = 0; i < StrLen; ++i) {
        if (x[i] != y[i]) return false;
    }
    return true;
}

template <size_t MemLen>
static bool
cls_eq_p(const WCHAR (&x) [MemLen], HWND wnd) {
    const auto MaxCls = 64;
    WCHAR cls_name[MaxCls];
    cls_name[MaxCls - 1] = 0;
    const auto len = GetClassName(wnd, cls_name, MaxCls);
    return str_eq_p(x, cls_name, len);
}

static RECT
window_geometry(const HWND wnd) {
    RECT rect;
    GetWindowRect(wnd, &rect);
    return rect;
}

static APPBARDATA
info_of_taskbar() {
    APPBARDATA info;
    info.cbSize = sizeof(APPBARDATA);
    SHAppBarMessage(ABM_GETTASKBARPOS, &info);
    return info;
}

static bool
autohide_enabled() {
    APPBARDATA info;
    info.cbSize = sizeof(APPBARDATA);
    const auto val = SHAppBarMessage(ABM_GETSTATE, &info);
    return (val & ABS_AUTOHIDE) != 0;
}

static MONITORINFO
minfo_of_hwnd(const HWND wnd) {
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    const auto monitor = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfo(monitor, &info);
    return info;
}

static bool
taskbar_visible_p(const HWND taskbar_hwnd) {
    const auto taskbar_info = info_of_taskbar();
    const auto taskbar = window_geometry(taskbar_hwnd);
    const auto monitor = minfo_of_hwnd(taskbar_hwnd);
    const auto maxdist = 4;
    switch (taskbar_info.uEdge) {
    case ABE_LEFT: return taskbar.right > (monitor.rcWork.left + maxdist);
    case ABE_TOP: return taskbar.bottom > (monitor.rcWork.top + maxdist);
    case ABE_RIGHT: return taskbar.left < (monitor.rcWork.right - maxdist);
    case ABE_BOTTOM: return taskbar.top < (monitor.rcWork.bottom - maxdist);
    default: return true;
    }
}

static void
show_taskbar(const HWND taskbar_hwnd) {
    handle_ty<rgn_ty> rgn { CreateRectRgn(0, 0, 0, 0) };
    const auto rgnres = GetWindowRgn(taskbar_hwnd, rgn.handle);
    if (rgnres == NULLREGION || rgnres == ERROR) return;
    SetWindowRgn(taskbar_hwnd, nullptr, true);
}

static RECT
box_from_rgn(const HWND wnd) {
    RECT ret = { 0 };
    handle_ty<rgn_ty> rgn { CreateRectRgn(0, 0, 0, 0) };
    const auto res = GetWindowRgn(wnd, rgn.handle);
    if (res == NULLREGION || res == ERROR) return ret;
    GetRgnBox(rgn.handle, &ret);
    return ret;
}

static void
hide_taskbar(const HWND taskbar_hwnd) {
    const auto geom = window_geometry(taskbar_hwnd);
    const auto margin = 2;

    const auto rel_right = geom.right - geom.left - margin;
    const auto rel_bottom = geom.bottom - geom.top - margin;

    const auto rgn_rect = box_from_rgn(taskbar_hwnd);
    const auto contained =
        rgn_rect.left >= margin &&
        rgn_rect.top >= margin &&
        rgn_rect.right <= rel_right &&
        rgn_rect.bottom <= rel_bottom;
    if (contained) return;

    const auto rgn = CreateRectRgn(margin, margin, rel_right, rel_bottom);
    SetWindowRgn(taskbar_hwnd, rgn, true);
}
