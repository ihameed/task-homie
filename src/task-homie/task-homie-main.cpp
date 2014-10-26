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

#pragma runtime_checks("", off)

#include "../task-homie-hook/task-homie-hook.hpp"

#include <shlwapi.h>

#include <tuple>

#include "resource.h"

#pragma warning(disable : 4510) // constructor could not be generated
#pragma warning(disable : 4610) // [...] can never be instantiated

extern "C" {

void * __cdecl memset(void *, int, size_t);
#pragma intrinsic(memset)

#pragma function(memset)
void *
memset(void * dst, int val, size_t sz) {
    for (size_t i = 0; i < sz; ++i) static_cast<char *>(dst)[i] = static_cast<char>(val);
    return dst;
}

}

namespace msg {
const auto TrayIcon = WM_APP;
const auto QuitRestart = WM_APP + 1;
}

const auto MenuExit = 0;

struct hook_ty final {
    using t = HHOOK;

    static bool
    is_valid(t handle) { return handle != nullptr; }

    static void
    invalidate(t &handle) { handle = nullptr; }

    static void
    destroy(t handle) { UnhookWindowsHookEx(handle); }
};

struct trayinfo_ty final { bool valid; NOTIFYICONDATA data; };

struct tray_ty final {
    using t = trayinfo_ty;

    static bool
    is_valid(const t &handle) { return handle.valid; }

    static void
    invalidate(t &handle) { handle.valid = false; }

    static void
    destroy(t &handle) { Shell_NotifyIcon(NIM_DELETE, &handle.data); }
};

using hook_handle_ty = handle_ty<hook_ty>;

using tray_handle_ty = handle_ty<tray_ty>;

using hooks_ty = std::tuple<hook_handle_ty, hook_handle_ty>;

struct exit_ty { int code; bool should_restart; };

int
failwith(const WCHAR * const reason) {
    OutputDebugString(L"task-homie: ");
    OutputDebugString(reason);
    OutputDebugString(L"\n");
    return 0;
}

template <size_t DstSz, size_t SrcSz>
static void
copy_wstr(WCHAR (&dst)[DstSz], const WCHAR (&src)[SrcSz]) {
    const auto sz = DstSz < SrcSz ? DstSz : SrcSz;
    for (size_t i = 0; i < sz; ++i) dst[i] = src[i];
    dst[DstSz - 1] = 0;
}

static HWND
find_taskbar() {
    HWND found = nullptr;
    enum_windows([&] (const HWND wnd) -> BOOL {
        if (cls_eq_p(TaskbarCls, wnd)) { found = wnd; return FALSE; }
        return TRUE;
    });
    return found;
}

static HICON
load_icon() {
    const auto mod = GetModuleHandle(nullptr);
    const auto size = GetSystemMetrics(SM_CXSMICON);
    const auto icon = reinterpret_cast<HICON>(LoadImage(
        mod, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, size, size,
        LR_DEFAULTCOLOR));
    return icon;
}

static trayinfo_ty
mk_notify_icon_data(const UINT id, const HWND wnd, const HICON icon) {
    trayinfo_ty ret = { 0 };
    ret.valid = true;
    auto &data = ret.data;
    data.cbSize = NOTIFYICONDATA_V2_SIZE;
    data.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    data.hIcon = icon;
    data.uCallbackMessage = msg::TrayIcon;
    data.uID = id;
    data.hWnd = wnd;
    data.uVersion = NOTIFYICON_VERSION;
    copy_wstr(data.szTip, L"Task Homie");
    return ret;
}

static tray_handle_ty
mk_systray_icon(const UINT id, const HWND wnd, const HICON icon) {
    auto data = mk_notify_icon_data(id, wnd, icon);
    if (!Shell_NotifyIcon(NIM_ADD, &data.data)) data.valid = false;
    else if (!Shell_NotifyIcon(NIM_SETVERSION, &data.data)) data.valid = false;
    return { data };
}

hooks_ty
mk_hooks(const HWND wnd, const HINSTANCE dylib, const HOOKPROC sync, const HOOKPROC async) {
    const auto tid = GetWindowThreadProcessId(wnd, nullptr);
    const auto mk = [&] (const int kind, const HOOKPROC proc) -> hook_handle_ty
        { return { SetWindowsHookEx(kind, proc, dylib, tid) }; };
    return hooks_ty { mk(WH_CALLWNDPROCRET, sync), mk(WH_GETMESSAGE, async) };
}

static HWND
mk_dummy_window() {
    return CreateWindow(L"static", L"task-homie-dummy", 0,
        0, 0, 1, 1,
        nullptr, nullptr, nullptr, nullptr);
}

static HMENU
mk_context_menu() {
    const auto menu = CreatePopupMenu();
    AppendMenu(menu, MF_STRING, MenuExit, L"E&xit");
    return menu;
}

template <typename f1, typename f2>
struct state_ty final {
    const HMENU menu;
    const HWND wnd;
    hooks_ty hooks;
    tray_handle_ty tray;
    const UINT taskbar_created_msg;
    const f1 & remake_hooks;
    const f2 & remake_tray;
    const bool restart_on_new_taskbar;
};

template <typename t>
static LRESULT CALLBACK
wnd_proc(const HWND wnd, const UINT msg, const WPARAM wparam, const LPARAM lparam) {
    auto &state = *reinterpret_cast<t *>(GetWindowLongPtr(wnd, GWLP_USERDATA));
    const auto show_context_menu = [&] {
        POINT pt;
        GetCursorPos(&pt);
        TrackPopupMenu(state.menu, TPM_RIGHTBUTTON, pt.x, pt.y,
            0, state.wnd, nullptr);
    };

    switch (msg) {
    case WM_COMMAND: {
        switch (LOWORD(wparam)) {
        case MenuExit: PostQuitMessage(0); break;
        }
    }
    break;

    case msg::TrayIcon:
        switch (LOWORD(lparam)) {
        case WM_RBUTTONUP:
        case NIN_KEYSELECT:
            SetForegroundWindow(wnd);
            show_context_menu();
            break;
        }
    break;

    default:
        if (msg == state.taskbar_created_msg) {
            if (state.restart_on_new_taskbar) {
                PostMessage(wnd, msg::QuitRestart, 0, 0);
            } else {
                state.tray = state.remake_tray();
                state.hooks = state.remake_hooks();
            }
        }
        break;
    }
    return DefWindowProc(wnd, msg, wparam, lparam);
}

static exit_ty
loop() { for (;;) {
    MSG msg;
    const auto msg_success = GetMessage(&msg, nullptr, 0, 0);
    if (msg_success == 0 && msg.message == WM_QUIT) {
        return { static_cast<int>(msg.wParam), false };
    }
    if (msg_success <= 0) return { 0, false };
    if (msg.message == msg::QuitRestart) return { 0, true };

    TranslateMessage(&msg);
    DispatchMessage(&msg);
}; }

template <size_t DstSz, size_t SrcSz>
bool
wstr_append(WCHAR (&dst)[DstSz], const WCHAR (&src)[SrcSz]) {
    const auto len = lstrlen(dst);
    if ((DstSz - len) < SrcSz) return false;
    if (lstrcat(dst, src) == nullptr) { dst[0] = 0; return false; }
    return true;
}

template <size_t Sz>
static bool
empty_path(WCHAR (&path)[Sz]) { path[0] = 0; return false; };

template <size_t Sz>
static bool
append_hook_path(WCHAR (&path)[Sz]) {
    PathRemoveFileSpec(path);
    if (!wstr_append(path, L"\\task-homie-hook.dll")) return empty_path(path);
    return true;
}

template <size_t Sz>
static bool
get_exe_path(WCHAR (&path)[Sz]) {
    path[Sz - 1] = 0;
    const auto end = GetModuleFileName(GetModuleHandle(nullptr), path, Sz - 1);
    if (end == 0) return empty_path(path);
    return true;
}

template <typename f1, typename f2>
static auto
only_once(const wchar_t * const tag, f1 && def, f2 && fun) -> decltype(fun()) {
    const auto mtx = CreateMutex(nullptr, FALSE, tag);
    const auto err = GetLastError();
    if (err != ERROR_SUCCESS) return def();
    const auto ret = fun();
    CloseHandle(mtx);
    return ret;
}

static bool
init_wndproc(const HWND wnd, void * const state, const WNDPROC wndproc) {
    const auto test = [&] (const int idx, void * const data) {
        SetLastError(0);
        SetWindowLongPtr(wnd, idx, reinterpret_cast<LONG_PTR>(data));
        if (GetLastError() != 0) return false;
        return true;
    };
    if (!test(GWLP_USERDATA, state)) return false;
    if (!test(GWLP_WNDPROC, wndproc)) return false;
    return true;
}

static void
set_dpi_aware() {
    const auto lib = LoadLibrary(L"user32.dll");
    if (lib == nullptr) return;
    using fun_ty = BOOL (WINAPI *) ();
    const auto fun = reinterpret_cast<fun_ty>(GetProcAddress(lib, "SetProcessDPIAware"));
    if (fun != nullptr) fun();
    FreeLibrary(lib);
}

static exit_ty
run_(const WCHAR * const dll_path) {
    const auto fail = [] (const WCHAR *msg)
        { return exit_ty { failwith(msg), false }; };
    set_dpi_aware();

    const auto lib = LoadLibrary(dll_path);
    if (lib == nullptr) return fail(L"LoadLibrary");

    const auto sync_fun = GetProcAddress(lib, "task_homie_filter_sync_messages");
    if (sync_fun == nullptr) return fail(L"GetProcAddress task_homie_filter_sync_messages");

    const auto async_fun = GetProcAddress(lib, "task_homie_filter_async_messages");
    if (async_fun == nullptr) return fail(L"GetProcAddress task_homie_filter_async_messages");

    const auto taskbar_created_msg = RegisterWindowMessage(L"TaskbarCreated");
    if (taskbar_created_msg == 0) return fail(L"RegisterWindowMessage TaskbarCreated");

    const auto icon = load_icon();
    if (icon == nullptr) return fail(L"load_icon");

    const auto dummy_wnd = mk_dummy_window();
    if (dummy_wnd == nullptr) return fail(L"mk_dummy_window");

    const auto menu = mk_context_menu();
    if (menu == nullptr) return fail(L"mk_context_menu");

    UINT id = 0;
    const auto remake_tray = [&] {
        ++id;
        return mk_systray_icon(id, dummy_wnd, icon);
    };

    const auto remake_hooks = [&] {
        return mk_hooks(find_taskbar(), lib,
            reinterpret_cast<HOOKPROC>(sync_fun),
            reinterpret_cast<HOOKPROC>(async_fun));
    };

    state_ty<decltype(remake_hooks), decltype(remake_tray)> state
        { menu
        , dummy_wnd
        , remake_hooks()
        , remake_tray()
        , taskbar_created_msg
        , remake_hooks
        , remake_tray
        , true
        };

    if (!init_wndproc(dummy_wnd, &state, &wnd_proc<decltype(state)>)) {
        return fail(L"init_wndproc failed!");
    }

    show_taskbar(find_taskbar());
    const auto ret = loop();
    show_taskbar(find_taskbar());
    return ret;
}

static void
start_process(const WCHAR * const exe_path) {
    PROCESS_INFORMATION process_info = { 0 };
    STARTUPINFO startup_info = { 0 };
    startup_info.cb = sizeof(STARTUPINFO);
    CreateProcess(exe_path, nullptr, nullptr, nullptr, FALSE, 0,
        nullptr, nullptr, &startup_info, &process_info);
}

const auto MaxPath = 65536;
static WCHAR dll_path[MaxPath] = { 0 };
static WCHAR exe_path[MaxPath] = { 0 };

static int
run() {
    if (!get_exe_path(dll_path)) return failwith(L"get_exe_path dll_path");
    if (!append_hook_path(dll_path)) return failwith(L"append_hook_path dll_path");
    if (!get_exe_path(exe_path)) return failwith(L"get_exe_path exe_path");

    const auto ret = only_once(
        L"task-homie-single-process-11cc0e01-31bf-426f-b2fa-2e52e9e426f8",
        [] { return exit_ty { 0, false }; },
        [] { return run_(dll_path); });
    if (ret.should_restart) { start_process(exe_path); }
    return ret.code;
}

extern "C" {

int CALLBACK
wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) { return run(); }

void
entry_point() { ExitProcess(run()); }

}
