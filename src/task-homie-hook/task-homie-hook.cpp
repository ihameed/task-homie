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

#include "task-homie-hook.hpp"

#include <atomic>

#ifndef _WIN64
#pragma comment(linker, "/EXPORT:task_homie_filter_async_messages=_task_homie_filter_async_messages@12")
#pragma comment(linker, "/EXPORT:task_homie_filter_sync_messages=_task_homie_filter_sync_messages@12")
#else
#pragma comment(linker, "/EXPORT:task_homie_filter_async_messages=task_homie_filter_async_messages")
#pragma comment(linker, "/EXPORT:task_homie_filter_sync_messages=task_homie_filter_sync_messages")
#endif

struct state_ty { HWND taskbar; };

const auto TaskSwitched = WM_USER + 243;

static const state_ty *
lazy_init_state();

template <typename t>
static void
filter_message(const t * const info) {
    const auto cond =
        // info->message == WM_WINDOWPOSCHANGED ||
        info->message == WM_MOVE ||
        info->message == TaskSwitched
        ;
    if (!cond) return;

    const auto state = lazy_init_state();
    if (state == nullptr) return;

    const auto taskbar = state->taskbar;
    if (taskbar == nullptr) return;
    if (taskbar != info->hwnd) return;

    if (!taskbar_visible_p(taskbar) && autohide_enabled()) hide_taskbar(taskbar);
    else show_taskbar(taskbar);
}

template <typename t>
static LRESULT
passthrough(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0) filter_message(reinterpret_cast<t *>(lparam));
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

const auto Uninitialized = reinterpret_cast<uintptr_t>(nullptr);
const auto Initializing = static_cast<uintptr_t>(1);

template <typename t, typename f>
static t *
lazy_init_ptr(std::atomic<t *> &init_status, f && init) {
    const auto raw = init_status.load(std::memory_order_consume);
    switch (reinterpret_cast<uintptr_t>(raw)) {
    case Uninitialized: {
        auto expected = reinterpret_cast<t *>(Uninitialized);
        const auto exchanged = init_status.compare_exchange_strong(
            expected, reinterpret_cast<t *>(Initializing),
            std::memory_order_release, std::memory_order_relaxed);
        if (!exchanged) return nullptr;
        else {
            init_status.store(init(), std::memory_order_release);
            return raw;
        }
    }

    case Initializing: return nullptr;

    default: return raw;
    }
}

static HWND
taskbar_of_current_process() {
    const auto pid = GetCurrentProcessId();
    HWND found = nullptr;
    enum_windows([&] (const HWND wnd) -> BOOL {
        DWORD wnd_pid;
        GetWindowThreadProcessId(wnd, &wnd_pid);
        const auto cls_eq = cls_eq_p(TaskbarCls, wnd);
        if (wnd_pid == pid && cls_eq) { found = wnd; return FALSE; }
        return TRUE;
    });
    return found;
}

static std::atomic<state_ty *> init_status;
static state_ty state;

static const state_ty *
lazy_init_state() {
    return lazy_init_ptr(init_status,
        [] { state.taskbar = taskbar_of_current_process(); return &state; });
}

extern "C" {

LRESULT CALLBACK
task_homie_filter_async_messages(int code, WPARAM wparam, LPARAM lparam)
{ return passthrough<MSG>(code, wparam, lparam); }

LRESULT CALLBACK
task_homie_filter_sync_messages(int code, WPARAM wparam, LPARAM lparam)
{ return passthrough<CWPRETSTRUCT>(code, wparam, lparam); }

BOOL WINAPI
DllMain(HINSTANCE, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        init_status = reinterpret_cast<state_ty *>(Uninitialized);
        return TRUE;
    }
    return TRUE;
}

}
