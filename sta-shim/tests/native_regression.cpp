// Tests the installed OS shim through ordinary user32 imports, never by including
// the shim source or invoking its private functions. Also runs on Server Core.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int failures = 0;
HINSTANCE instance = GetModuleHandleW(nullptr);
constexpr UINT testMessage = WM_APP + 41;
constexpr UINT threadMessage = WM_APP + 42;
constexpr UINT slowMessage = WM_APP + 43;
constexpr UINT selfDestroyMessage = WM_APP + 44;
constexpr UINT nestedMessage = WM_APP + 45;
std::vector<std::pair<HWND, UINT>> lifecycle;
HWND lifecycleParent = nullptr;
HWND lifecycleChild = nullptr;
bool creationContext = false;
bool parentBeforeChildren = false;
bool parentAfterChildren = false;
int lifecycleDispatches = 0;
bool runningShim = false;
std::atomic<int> callbackCount{0};

void Check(const char* name, bool success) {
    std::printf("CHECK %s %s\n", name, success ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!success) ++failures;
}

void CheckWaitArgumentFailure(const char* name, DWORD result) {
    DWORD error = GetLastError();
    std::printf("OBSERVE %s result=0x%08lX error=%lu\n", name, result, error);
    // The Windows WOW64 control returns STATUS_UNSUCCESSFUL without a Win32
    // error for these invalid arguments. The shim must keep its exact contract.
    bool wow64Control = !runningShim && sizeof(void*) == 4 &&
        result == 0xC0000001u && error == ERROR_SUCCESS;
    Check(name, (result == WAIT_FAILED && error == ERROR_INVALID_PARAMETER) || wow64Control);
}

void Require(bool condition, const char* operation) {
    if (!condition) {
        std::printf("FATAL %s err=%lu\n", operation, GetLastError());
        std::fflush(stdout);
        ExitProcess(2);
    }
}

LRESULT CALLBACK BasicProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == testMessage) return 11;
    if (message == threadMessage) return GetCurrentThreadId();
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK OtherProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == testMessage) return 22;
    return DefWindowProcW(window, message, wParam, lParam);
}

ATOM Register(const wchar_t* name, WNDPROC proc = BasicProc, int extra = 0,
              HINSTANCE module = instance, UINT style = 0) {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpszClassName = name;
    cls.hInstance = module;
    cls.lpfnWndProc = proc;
    cls.cbWndExtra = extra;
    cls.style = style;
    return RegisterClassExW(&cls);
}

HWND Create(const wchar_t* name, HWND parent = HWND_MESSAGE, DWORD style = 0,
            void* parameter = nullptr, HINSTANCE module = instance) {
    return CreateWindowExW(0, name, L"roundtable", style, 1, 2, 3, 4, parent, nullptr, module, parameter);
}

void Drain() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message != WM_QUIT) DispatchMessageW(&message);
    }
}

LRESULT CALLBACK LifecycleProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == testMessage) {
        ++lifecycleDispatches;
        return 42;
    }
    if (message == WM_NCCREATE || message == WM_CREATE || message == WM_DESTROY || message == WM_NCDESTROY) {
        lifecycle.emplace_back(window, message);
    }
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        creationContext = creation->lpCreateParams == reinterpret_cast<void*>(0x1234) &&
            creation->hInstance == instance && creation->x == 1 && creation->y == 2 &&
            creation->cx == 3 && creation->cy == 4;
    }
    if (window == lifecycleParent && message == WM_DESTROY) {
        parentBeforeChildren = IsWindow(lifecycleChild) != FALSE;
        if (runningShim) DestroyWindow(window); // Also test the shim's reentrancy guard.
    }
    if (window == lifecycleParent && message == WM_NCDESTROY) parentAfterChildren = !IsWindow(lifecycleChild);
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK RejectNonClient(HWND, UINT message, WPARAM, LPARAM) {
    return message == WM_NCCREATE ? FALSE : 0;
}

LRESULT CALLBACK RejectCreate(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) return -1;
    return DefWindowProcW(window, message, wParam, lParam);
}

void TestLifecycleAndExtraBytes() {
    Require(Register(L"RTLifecycle", LifecycleProc, 16) != 0, "register lifecycle");
    lifecycleParent = Create(L"RTLifecycle", HWND_MESSAGE, 0, reinterpret_cast<void*>(0x1234));
    Require(lifecycleParent != nullptr, "create lifecycle");
    Check("R03_creation_context_and_notifications", creationContext && lifecycle.size() == 2 &&
        lifecycle[0].second == WM_NCCREATE && lifecycle[1].second == WM_CREATE &&
        GetWindowLongPtrW(lifecycleParent, GWLP_USERDATA) == 0x1234);
    SetWindowLongW(lifecycleParent, 4, 0x22222222);
    SetWindowLongW(lifecycleParent, 0, 0x11111111);
    Check("R04_LONG_width", GetWindowLongW(lifecycleParent, 4) == 0x22222222);
    LONG_PTR token = sizeof(void*) == 8 ? static_cast<LONG_PTR>(0x1122334455667788ULL) : 0x55667788;
    SetWindowLongPtrW(lifecycleParent, 8, token);
    Check("R04_pointer_width", GetWindowLongPtrW(lifecycleParent, 8) == token);
    SetLastError(0);
    LONG invalid = SetWindowLongW(lifecycleParent, 15, 123);
    Check("R04_set_bounds", invalid == 0 && GetLastError() == ERROR_INVALID_INDEX);
    SetLastError(0);
    Check("R04_get_bounds", GetWindowLongPtrW(lifecycleParent, 16) == 0 && GetLastError() == ERROR_INVALID_INDEX);
    SetLastError(0);
    Check("R04_negative_index", GetWindowLongW(lifecycleParent, -999) == 0 && GetLastError() == ERROR_INVALID_INDEX);
    SetLastError(0);
    Check("R04_negative_extra", Register(L"RTNegativeExtra", BasicProc, -1) == 0 && GetLastError() == ERROR_INVALID_PARAMETER);
    lifecycleChild = Create(L"RTLifecycle", lifecycleParent, WS_CHILD, reinterpret_cast<void*>(0x1234));
    Require(lifecycleChild != nullptr, "create child");
    HWND grandchild = Create(L"RTLifecycle", lifecycleChild, WS_CHILD, reinterpret_cast<void*>(0x1234));
    Require(grandchild != nullptr, "create grandchild");
    Check("N01_ancestry", IsChild(lifecycleParent, grandchild) && !IsChild(grandchild, lifecycleParent) &&
        !IsChild(lifecycleParent, lifecycleParent));
    PostMessageW(grandchild, testMessage, 0, 0);
    MSG message{};
    Check("N01_descendant_filter", PeekMessageW(&message, lifecycleParent, testMessage, testMessage, PM_REMOVE) &&
        message.hwnd == grandchild);
    bool foreignDenied = false;
    std::thread foreign([&] {
        SetLastError(0);
        foreignDenied = !DestroyWindow(lifecycleParent) && GetLastError() == ERROR_ACCESS_DENIED;
    });
    foreign.join();
    Check("R03_owner_only_destroy", foreignDenied && IsWindow(lifecycleParent));
    Check("R03_live_dispatch_control", SendMessageW(lifecycleParent, testMessage, 0, 0) == 42 &&
        lifecycleDispatches == 1);
    PostMessageW(lifecycleParent, testMessage, 0, 0);
    PostMessageW(lifecycleParent, WM_TIMER, 7, 0);
    Require(DestroyWindow(lifecycleParent) != FALSE, "destroy parent");
    Check("R03_parent_child_destroy_order", parentBeforeChildren && parentAfterChildren &&
        !IsWindow(lifecycleParent) && !IsWindow(lifecycleChild) && !IsWindow(grandchild));
    Check("R03_destroy_purges_posts_and_timers", !PeekMessageW(&message, nullptr, testMessage, testMessage, PM_REMOVE) &&
        !PeekMessageW(&message, nullptr, WM_TIMER, WM_TIMER, PM_REMOVE));
    size_t notifications = lifecycle.size();
    message = {};
    message.hwnd = lifecycleParent;
    message.message = testMessage;
    DispatchMessageW(&message);
    Check("R03_no_dispatch_after_destroy", lifecycleDispatches == 1 && lifecycle.size() == notifications);
    SetLastError(0);
    Check("R03_unknown_class", Create(L"RTNeverRegistered") == nullptr && GetLastError() == ERROR_CANNOT_FIND_WND_CLASS);
    HWND predefined = Create(L"STATIC");
    Check("R03_predefined_class", predefined != nullptr);
    if (predefined) DestroyWindow(predefined);
    Require(Register(L"RTRejectNC", RejectNonClient) != 0, "register NC rejection");
    Require(Register(L"RTRejectCreate", RejectCreate) != 0, "register create rejection");
    Check("R03_creation_rejection", Create(L"RTRejectNC") == nullptr && Create(L"RTRejectCreate") == nullptr);
    HWND close = Create(L"RTBasic");
    Require(close != nullptr, "create close target");
    SendMessageW(close, WM_CLOSE, 0, 0);
    Check("R03_default_close", !IsWindow(close));
}

WNDPROC previousProc = nullptr;
LRESULT CALLBACK SubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    LRESULT result = CallWindowProcW(previousProc, window, message, wParam, lParam);
    return message == testMessage ? result + 1 : result;
}

void TestClasses() {
    const wchar_t* name = L"RTScoped";
    HINSTANCE other = GetModuleHandleW(L"kernel32.dll");
    ATOM firstAtom = Register(name, BasicProc, 8);
    Require(firstAtom != 0, "register local class");
    SetLastError(0);
    Check("R10_duplicate_registration", Register(name, OtherProc) == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    ATOM secondAtom = Register(name, OtherProc, 12, other);
    Require(secondAtom != 0, "register other instance");
    Check("R10_same_name_shared_atom", firstAtom == secondAtom);
    HWND first = Create(name);
    HWND second = Create(name, HWND_MESSAGE, 0, nullptr, other);
    Require(first && second, "create scoped windows");
    Check("R10_instance_scoping", SendMessageW(first, testMessage, 0, 0) == 11 &&
        SendMessageW(second, testMessage, 0, 0) == 22);
    LPCWSTR atomName = reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(firstAtom));
    HWND crossAtom = Create(atomName, HWND_MESSAGE, 0, nullptr, other);
    Check("R10_atom_instance_scoping", crossAtom && SendMessageW(crossAtom, testMessage, 0, 0) == 22);
    if (crossAtom) DestroyWindow(crossAtom);
    HWND unqualified = Create(name, HWND_MESSAGE, 0, nullptr, nullptr);
    HWND unqualifiedAtom = Create(atomName, HWND_MESSAGE, 0, nullptr, nullptr);
    Check("R10_null_instance_creation", unqualified && SendMessageW(unqualified, testMessage, 0, 0) == 22 &&
        unqualifiedAtom && SendMessageW(unqualifiedAtom, testMessage, 0, 0) == 22);
    if (unqualified) DestroyWindow(unqualified);
    if (unqualifiedAtom) DestroyWindow(unqualifiedAtom);
    SetLastError(0);
    Check("R10_wrong_instance_creation", Create(name, HWND_MESSAGE, 0, nullptr, GetModuleHandleW(L"ntdll.dll")) == nullptr &&
        GetLastError() == ERROR_CANNOT_FIND_WND_CLASS);
    WNDCLASSEXW extended{};
    extended.cbSize = sizeof(extended);
    WNDCLASSW basic{};
    BOOL extendedAtom = GetClassInfoExW(instance, name, &extended);
    BOOL basicAtom = GetClassInfoW(other, name, &basic);
    Check("R10_class_queries", extendedAtom && extended.cbWndExtra == 8 &&
        extended.lpfnWndProc == BasicProc && basicAtom && basic.cbWndExtra == 12 &&
        basic.lpfnWndProc == OtherProc);
    HWND queriedAtom = Create(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(extendedAtom)));
    Check("R10_query_returns_usable_atom", extendedAtom == firstAtom && basicAtom == secondAtom &&
        queriedAtom && SendMessageW(queriedAtom, testMessage, 0, 0) == 11);
    if (queriedAtom) DestroyWindow(queriedAtom);
    SetLastError(0);
    Check("R10_null_instance_query_is_not_wildcard", !GetClassInfoExW(nullptr, name, &extended) &&
        GetLastError() == ERROR_CLASS_DOES_NOT_EXIST);
    SetLastError(0);
    Check("R10_in_use_unregister", !UnregisterClassW(name, instance) && GetLastError() == ERROR_CLASS_HAS_WINDOWS);
    previousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(first, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubclassProc)));
    Check("N03_subclass_chain", previousProc == BasicProc && SendMessageW(first, testMessage, 0, 0) == 12);
    DestroyWindow(first);
    Check("R10_unregister_one_scope_with_other_live", UnregisterClassW(name, instance) &&
        SendMessageW(second, testMessage, 0, 0) == 22);
    HWND survivingAtom = Create(atomName, HWND_MESSAGE, 0, nullptr, other);
    Check("R10_shared_atom_survives_scope_removal", survivingAtom && SendMessageW(survivingAtom, testMessage, 0, 0) == 22);
    if (survivingAtom) DestroyWindow(survivingAtom);
    DestroyWindow(second);
    Check("R10_unregister_scopes", UnregisterClassW(name, other) != FALSE);
    SetLastError(0);
    Check("R10_missing_unregister", !UnregisterClassW(name, instance) && GetLastError() == ERROR_CLASS_DOES_NOT_EXIST);
    Require(Register(L"RTGlobal", BasicProc, 0, instance, CS_GLOBALCLASS) != 0, "register global");
    Require(Register(L"RTGlobal", OtherProc, 0, other) != 0, "register local over global");
    HWND local = Create(L"RTGlobal", HWND_MESSAGE, 0, nullptr, other);
    HWND global = Create(L"RTGlobal");
    Require(local && global, "create global and local");
    Check("R10_local_before_global", SendMessageW(local, testMessage, 0, 0) == 22 && SendMessageW(global, testMessage, 0, 0) == 11);
    DestroyWindow(local);
    DestroyWindow(global);
    UnregisterClassW(L"RTGlobal", other);
    UnregisterClassW(L"RTGlobal", instance);
    Require(Register(L"RTNullInstance", BasicProc, 0, nullptr) != 0, "register null instance");
    Check("R10_null_registration_uses_application", GetClassInfoW(instance, L"RTNullInstance", &basic) &&
        basic.hInstance == instance && basic.lpfnWndProc == BasicProc);
    Require(UnregisterClassW(L"RTNullInstance", instance) != FALSE, "unregister normalized instance");
    // Combase registers this name itself. It must not be a predefined stub.
    Require(Register(L"OleMainThreadWndClass", BasicProc) != 0, "register OLE class name");
    HWND ole = Create(L"OleMainThreadWndClass");
    Check("R03_R10_registered_OLE_class", ole && SendMessageW(ole, testMessage, 0, 0) == 11);
    if (ole) DestroyWindow(ole);
    UnregisterClassW(L"OleMainThreadWndClass", instance);
    ATOM anchor = Register(L"RTAtomAnchor");
    Require(anchor != 0, "register anchor atom");
    bool noCollision = true;
    for (UINT i = 0; i < 0x4001; ++i) {
        wchar_t temp[40]{};
        swprintf_s(temp, L"RTAtom_%u", i);
        ATOM atom = Register(temp);
        if (!atom || atom == anchor || !UnregisterClassW(temp, instance)) { noCollision = false; break; }
    }
    Check("R10_freed_atom_reuse_without_collision", noCollision);
    HWND byAtom = Create(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(anchor)));
    Check("R10_atom_lookup", byAtom != nullptr);
    if (byAtom) DestroyWindow(byAtom);
    UnregisterClassW(L"RTAtomAnchor", instance);
}

struct Payload {
    std::atomic<int> Calls{0};
    std::atomic<DWORD> Thread{0};
    HANDLE Entered = nullptr;
    HANDLE Release = nullptr;
    LONG Magic = 0x12345678;
};

LRESULT CALLBACK SendProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == nestedMessage) return SendMessageW(reinterpret_cast<HWND>(wParam), threadMessage, 0, 0);
    if (message == slowMessage || message == selfDestroyMessage) {
        auto* payload = reinterpret_cast<Payload*>(lParam);
        ++payload->Calls;
        payload->Thread = GetCurrentThreadId();
        if (message == selfDestroyMessage) {
            DestroyWindow(window);
            SetEvent(payload->Entered);
            WaitForSingleObject(payload->Release, 5000);
        } else {
            Sleep(800);
        }
        return payload->Magic == 0x12345678 ? 0x5A5A : 0;
    }
    return BasicProc(window, message, wParam, lParam);
}

struct Owner {
    HANDLE Ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE Permit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HWND Window = nullptr;
    DWORD ThreadId = 0;
    int Mode = 0; // Pump, destroy without dispatch, or exit without dispatch.
    ~Owner() { CloseHandle(Ready); CloseHandle(Permit); }
};

std::thread StartOwner(Owner& owner) {
    return std::thread([&owner] {
        owner.ThreadId = GetCurrentThreadId();
        owner.Window = Create(L"RTSend");
        SetEvent(owner.Ready);
        WaitForSingleObject(owner.Permit, 10000);
        if (owner.Mode == 2) return;
        if (owner.Mode == 1) { DestroyWindow(owner.Window); return; }
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
        if (IsWindow(owner.Window)) DestroyWindow(owner.Window);
    });
}

void TestSendOwnership() {
    Require(Register(L"RTSend", SendProc) != 0, "register send");
    Owner owner;
    auto receiver = StartOwner(owner);
    Require(WaitForSingleObject(owner.Ready, 3000) == WAIT_OBJECT_0 && owner.Window, "receiver ready");
    Payload payload;
    HANDLE complete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    LRESULT result = 0;
    ULONGLONG started = GetTickCount64();
    std::thread sender([&] {
        result = SendMessageW(owner.Window, slowMessage, 0, reinterpret_cast<LPARAM>(&payload));
        SetEvent(complete);
    });
    Check("R01_no_wrong_thread_fallback", WaitForSingleObject(complete, 100) == WAIT_TIMEOUT);
    SetEvent(owner.Permit);
    sender.join();
    Check("R01_slow_send_once_on_owner", result == 0x5A5A && payload.Calls == 1 &&
        payload.Thread == owner.ThreadId && GetTickCount64() - started >= 800);
    HWND peer = Create(L"RTBasic");
    Require(peer != nullptr, "nested send peer");
    Check("R01_reentrant_send", SendMessageW(owner.Window, nestedMessage, reinterpret_cast<WPARAM>(peer), 0) == GetCurrentThreadId());
    DestroyWindow(peer);
    Payload self;
    self.Entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    self.Release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ResetEvent(complete);
    std::thread selfSender([&] {
        result = SendMessageW(owner.Window, selfDestroyMessage, 0, reinterpret_cast<LPARAM>(&self));
        SetEvent(complete);
    });
    Require(WaitForSingleObject(self.Entered, 3000) == WAIT_OBJECT_0, "self-destroy handler entered");
    Check("R01_destroy_during_dispatch_keeps_sender_blocked", WaitForSingleObject(complete, 100) == WAIT_TIMEOUT);
    SetEvent(self.Release);
    selfSender.join();
    Check("R01_sender_frame_lifetime", result == 0x5A5A && self.Calls == 1 && self.Thread == owner.ThreadId);
    PostThreadMessageW(owner.ThreadId, WM_QUIT, 0, 0);
    receiver.join();
    CloseHandle(self.Entered);
    CloseHandle(self.Release);
    for (int mode : {1, 2}) {
        Owner abandoned;
        abandoned.Mode = mode;
        auto target = StartOwner(abandoned);
        Require(WaitForSingleObject(abandoned.Ready, 3000) == WAIT_OBJECT_0 && abandoned.Window, "abandoned receiver ready");
        Payload pending;
        ResetEvent(complete);
        std::thread pendingSender([&] {
            result = SendMessageW(abandoned.Window, slowMessage, 0, reinterpret_cast<LPARAM>(&pending));
            SetEvent(complete);
        });
        Check(mode == 1 ? "R01_pending_destroy_blocks" : "R08_pending_exit_blocks",
            WaitForSingleObject(complete, 100) == WAIT_TIMEOUT);
        SetEvent(abandoned.Permit);
        target.join();
        pendingSender.join();
        Check(mode == 1 ? "R01_pending_destroy_cancels" : "R08_pending_exit_cancels",
            result == 0 && pending.Calls == 0 && !IsWindow(abandoned.Window));
        SetLastError(0);
        Check("R08_dead_window_post_rejected", !PostMessageW(abandoned.Window, testMessage, 0, 0) &&
            GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    }
    CloseHandle(complete);
}

unsigned long long ThreadCpuMs(HANDLE thread) {
    FILETIME created{}, exited{}, kernel{}, user{};
    Require(GetThreadTimes(thread, &created, &exited, &kernel, &user) != FALSE, "read thread CPU");
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart) / 10000;
}

void TestQueueLifetimeAndFilteredWait() {
    SetLastError(0);
    Check("R08_invalid_thread", !PostThreadMessageW(0xFFFFFFF0, testMessage, 0, 0) && GetLastError() == ERROR_INVALID_THREAD_ID);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD noQueueId = 0;
    std::thread noQueue([&] {
        noQueueId = GetCurrentThreadId();
        SetEvent(ready);
        WaitForSingleObject(release, 3000);
    });
    Require(WaitForSingleObject(ready, 3000) == WAIT_OBJECT_0, "no-queue thread ready");
    SetLastError(0);
    Check("R08_live_thread_without_queue", !PostThreadMessageW(noQueueId, testMessage, 0, 0) && GetLastError() == ERROR_INVALID_THREAD_ID);
    SetEvent(release);
    noQueue.join();
    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    bool removed = true;
    for (int i = 0; i < 64; ++i) {
        HWND stale = nullptr;
        std::thread transient([&] {
            MSG ignored{};
            PeekMessageW(&ignored, nullptr, 0, 0, PM_NOREMOVE);
            stale = Create(L"RTBasic");
            SetTimer(stale, 9, 10, nullptr);
        });
        transient.join();
        removed = removed && stale && !IsWindow(stale);
    }
    GetProcessHandleCount(GetCurrentProcess(), &after);
    Check("R08_thread_windows_removed", removed);
    Check("R08_queue_handles_released", after <= before + 2);
    ResetEvent(ready);
    HWND filtered = nullptr;
    bool matched = false;
    std::thread waiter([&] {
        filtered = Create(L"RTBasic");
        PostMessageW(filtered, testMessage, 0, 0);
        SetEvent(ready);
        MSG message{};
        matched = GetMessageW(&message, filtered, testMessage + 1, testMessage + 1) > 0 &&
            message.message == testMessage + 1;
        DestroyWindow(filtered);
    });
    Require(WaitForSingleObject(ready, 3000) == WAIT_OBJECT_0, "filtered waiter ready");
    auto cpuBefore = ThreadCpuMs(reinterpret_cast<HANDLE>(waiter.native_handle()));
    Sleep(300);
    auto cpuAfter = ThreadCpuMs(reinterpret_cast<HANDLE>(waiter.native_handle()));
    PostMessageW(filtered, testMessage + 1, 0, 0);
    waiter.join();
    Check("R07_filtered_getmessage_blocks", matched && cpuAfter - cpuBefore < 100);
    CloseHandle(ready);
    CloseHandle(release);
}

VOID CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) { ++callbackCount; }
VOID CALLBACK ApcProc(ULONG_PTR) { ++callbackCount; }

void TestTimersWaitsAndQuit() {
    Drain();
    HWND window = Create(L"RTBasic");
    Require(window != nullptr, "timer window");
    GetQueueStatus(QS_ALLINPUT | QS_ALLPOSTMESSAGE);
    PostMessageW(window, testMessage, 0, 0);
    Check("R06_queue_status_mask", GetQueueStatus(QS_TIMER) == 0);
    DWORD first = GetQueueStatus(QS_POSTMESSAGE);
    DWORD second = GetQueueStatus(QS_POSTMESSAGE);
    Check("R06_queue_status_changed_and_current", LOWORD(first) == QS_POSTMESSAGE && HIWORD(first) == QS_POSTMESSAGE &&
        LOWORD(second) == 0 && HIWORD(second) == QS_POSTMESSAGE);
    Check("R06_zero_wake_mask", MsgWaitForMultipleObjectsEx(0, nullptr, 30, 0, MWMO_INPUTAVAILABLE) == WAIT_TIMEOUT);
    Check("R06_timer_mask", MsgWaitForMultipleObjectsEx(0, nullptr, 30, QS_TIMER, MWMO_INPUTAVAILABLE) == WAIT_TIMEOUT);
    Check("R06_old_input", MsgWaitForMultipleObjectsEx(0, nullptr, 0, QS_POSTMESSAGE, 0) == WAIT_TIMEOUT);
    Check("R06_input_available", MsgWaitForMultipleObjectsEx(0, nullptr, 0, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0);
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check("R06_waitall_unsignaled", MsgWaitForMultipleObjectsEx(1, &event, 30, QS_POSTMESSAGE,
        MWMO_WAITALL | MWMO_INPUTAVAILABLE) == WAIT_TIMEOUT);
    SetEvent(event);
    DWORD all = MsgWaitForMultipleObjectsEx(1, &event, 30, QS_POSTMESSAGE, MWMO_WAITALL | MWMO_INPUTAVAILABLE);
    Check("R06_waitall_signaled", all == WAIT_OBJECT_0);
    SetLastError(0);
    CheckWaitArgumentFailure("R06_invalid_count", MsgWaitForMultipleObjectsEx(64, nullptr, 0, QS_ALLINPUT, 0));
    SetLastError(0);
    CheckWaitArgumentFailure("R06_null_handles", MsgWaitForMultipleObjectsEx(1, nullptr, 0, QS_ALLINPUT, 0));
    if (runningShim) {
        // The roundtable limits the shim to the three documented MWMO flags.
        SetLastError(0);
        Check("R06_invalid_flags", MsgWaitForMultipleObjectsEx(0, nullptr, 0, QS_ALLINPUT, 8) == WAIT_FAILED &&
            GetLastError() == ERROR_INVALID_PARAMETER);
    }
    Drain();
    Require(QueueUserAPC(ApcProc, GetCurrentThread(), 0) != 0, "queue alertable APC");
    Check("R06_alertable_wait", MsgWaitForMultipleObjectsEx(0, nullptr, 100, 0, MWMO_ALERTABLE) == WAIT_IO_COMPLETION);
    UINT_PTR timer = SetTimer(window, 7, 1000, nullptr);
    Require(timer != 0, "set replacement timer");
    Check("R05_timer_replacement", SetCoalescableTimer(window, timer, 10, TimerProc, 0) == timer);
    Check("R05_wait_bounded_by_timer", MsgWaitForMultipleObjectsEx(0, nullptr, 1000, QS_TIMER, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0);
    PostMessageW(window, testMessage, 0, 0);
    MSG message{}, again{};
    Check("R05_low_priority", PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) && message.message == testMessage);
    Check("R05_timer_noremove", PeekMessageW(&message, nullptr, WM_TIMER, WM_TIMER, PM_NOREMOVE) &&
        PeekMessageW(&again, nullptr, WM_TIMER, WM_TIMER, PM_NOREMOVE) && message.wParam == again.wParam);
    Require(PeekMessageW(&message, nullptr, WM_TIMER, WM_TIMER, PM_REMOVE) != FALSE, "remove due timer");
    int callbacks = callbackCount;
    DispatchMessageW(&message);
    Check("R05_timer_callback", message.message == WM_TIMER && message.wParam == timer && callbackCount == callbacks + 1);
    Check("R05_kill_after_tick", KillTimer(window, timer) != FALSE);
    SetLastError(0);
    Check("R05_kill_unknown", !KillTimer(window, timer) && (!runningShim || GetLastError() == ERROR_INVALID_PARAMETER));
    UINT_PTR threadTimer = SetTimer(nullptr, 0, 0, nullptr);
    Require(threadTimer != 0, "set thread timer");
    Check("R05_thread_timer_replacement", SetTimer(nullptr, threadTimer, 10, TimerProc) == threadTimer);
    Check("R05_kill_before_tick", KillTimer(nullptr, threadTimer) != FALSE);
    Check("R05_killed_timer_no_wake", MsgWaitForMultipleObjectsEx(0, nullptr, 30, QS_TIMER, MWMO_INPUTAVAILABLE) == WAIT_TIMEOUT);
    timer = SetTimer(window, 8, 10, nullptr);
    MSG timed{};
    ULONGLONG began = GetTickCount64();
    Check("R05_getmessage_timer", GetMessageW(&timed, window, WM_TIMER, WM_TIMER) > 0 &&
        timed.message == WM_TIMER && timed.wParam == timer && GetTickCount64() - began < 1000);
    KillTimer(window, timer);
    Drain();
    PostMessageW(window, testMessage, 0, 0);
    PostQuitMessage(17);
    Check("R09_posted_before_quit", PeekMessageW(&message, window, 0, 0, PM_REMOVE) && message.message == testMessage);
    Check("R09_quit_ignores_filters_noremove", PeekMessageW(&message, window, WM_APP, WM_APP + 100, PM_NOREMOVE) &&
        message.message == WM_QUIT && message.wParam == 17 &&
        PeekMessageW(&again, window, WM_APP, WM_APP + 100, PM_NOREMOVE) && again.message == WM_QUIT);
    Check("R09_getmessage_quit", GetMessageW(&message, window, WM_APP, WM_APP + 100) == 0 && message.wParam == 17);
    if (runningShim) {
        // Explicitly posted WM_QUIT is folded into quit state by the agreed shim policy.
        PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, 18, 0);
        Check("R09_explicit_thread_quit", PeekMessageW(&message, window, WM_APP, WM_APP + 100, PM_REMOVE) &&
            message.message == WM_QUIT && message.wParam == 18);
    }
    DestroyWindow(window);
    CloseHandle(event);
}

void TestUserObjectsAndClipboard(bool shim) {
    USEROBJECTFLAGS flags;
    std::memset(&flags, 0xAB, sizeof(flags));
    DWORD needed = 0;
    HANDLE station = GetProcessWindowStation();
    Check("R20_flags_initialized", GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed) &&
        needed == sizeof(flags) && !flags.fInherit && !flags.fReserved && (!shim || flags.dwFlags == 0));
    SetLastError(0);
    Check("R20_short_buffer", !GetUserObjectInformationW(station, UOI_FLAGS, &flags, 1, &needed) &&
        needed == sizeof(flags) && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    SetLastError(0);
    BOOL unknown = GetUserObjectInformationW(station, 999, &flags, sizeof(flags), &needed);
    DWORD unknownError = GetLastError();
    std::printf("OBSERVE R20_unknown_index result=%d error=%lu\n", unknown, unknownError);
    bool wow64Control = !shim && sizeof(void*) == 4 && unknownError == ERROR_SUCCESS;
    Check("R20_unknown_index", !unknown && (unknownError == ERROR_INVALID_PARAMETER || wow64Control));
    UINT first = RegisterClipboardFormatW(L"RTRange_0");
    Check("N05_format_range_and_reuse", first >= 0xC000 && first <= 0xFFFF &&
        RegisterClipboardFormatW(L"RTRange_0") == first);
    if (!shim) return; // The real clipboard atom table is shared with other processes.
    Check("N05_first_format", first == 0xC000);
    bool bounded = true;
    UINT last = first;
    for (UINT i = 1; i < 0x4000; ++i) {
        wchar_t name[40]{};
        swprintf_s(name, L"RTRange_%u", i);
        last = RegisterClipboardFormatW(name);
        if (last != 0xC000 + i) { bounded = false; break; }
    }
    SetLastError(0);
    UINT exhausted = RegisterClipboardFormatW(L"RTRange_exhausted");
    Check("N05_exhaustion", bounded && last == 0xFFFF && exhausted == 0 && GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
    Check("N05_reuse_after_exhaustion", RegisterClipboardFormatW(L"RTRange_0") == first);
}

LRESULT CALLBACK BasicProcA(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == testMessage) return 11;
    return DefWindowProcA(window, message, wParam, lParam);
}

struct DelayDescriptor {
    DWORD Attributes, DllName, ModuleHandle, Iat, Names, BoundIat, UnloadIat, Timestamp;
};

void* EffectiveUser32Target(HMODULE module, const char* name) {
    BYTE* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    if (rva) {
        auto* descriptor = reinterpret_cast<DelayDescriptor*>(base + rva);
        for (; descriptor->DllName; ++descriptor) {
            auto* names = reinterpret_cast<ULONG_PTR*>(base + descriptor->Names);
            auto* slots = reinterpret_cast<void**>(base + descriptor->Iat);
            for (; *names; ++names, ++slots) {
                ULONG_PTR ordinal = static_cast<ULONG_PTR>(1) << (sizeof(ULONG_PTR) * 8 - 1);
                if ((*names & ordinal) == 0 && std::strcmp(reinterpret_cast<char*>(base + *names + 2), name) == 0) return *slots;
            }
        }
    }
    return reinterpret_cast<void*>(GetProcAddress(module, name));
}

void TestAnsiAndHeadlessGdi(bool shim) {
    WNDCLASSEXA clsA{};
    clsA.cbSize = sizeof(clsA);
    clsA.lpszClassName = "RTAnsiClass";
    clsA.hInstance = instance;
    clsA.lpfnWndProc = BasicProcA;
    clsA.cbWndExtra = 16;
    ATOM atomA = RegisterClassExA(&clsA);
    Check("VB6_ansi_register_class", atomA != 0);
    WNDCLASSEXA queriedA{};
    queriedA.cbSize = 0xCCCCCCCCu; // msvbvm60.dll calls GetClassInfoExA("VBBubble") without initializing cbSize.
    Check("VB6_ansi_class_info", GetClassInfoExA(instance, "RTAnsiClass", &queriedA) &&
        queriedA.cbWndExtra == 16 && queriedA.lpfnWndProc == BasicProcA);
    HWND hwndA = CreateWindowExA(0, "RTAnsiClass", "ansi-window", 0, 1, 2, 3, 4, HWND_MESSAGE, nullptr, instance, nullptr);
    Check("VB6_ansi_create_window", hwndA != nullptr);
    if (hwndA) {
        char classNameBuf[64]{};
        int classNameLen = GetClassNameA(hwndA, classNameBuf, sizeof(classNameBuf));
        Check("VB6_ansi_class_name", classNameLen > 0 && std::strcmp(classNameBuf, "RTAnsiClass") == 0);
        SetWindowLongA(hwndA, 0, 0x13572468);
        Check("VB6_ansi_window_long", GetWindowLongA(hwndA, 0) == 0x13572468);
        Check("VB6_ansi_send_message", SendMessageA(hwndA, testMessage, 0, 0) == 11);
        PostMessageA(hwndA, testMessage, 77, 88);
        MSG msgA{};
        Check("VB6_ansi_post_peek_dispatch", PeekMessageA(&msgA, hwndA, testMessage, testMessage, PM_REMOVE) &&
            msgA.wParam == 77 && msgA.lParam == 88 && DispatchMessageA(&msgA) == 11);
        DestroyWindow(hwndA);
    }
    Check("VB6_ansi_unregister_class", UnregisterClassA("RTAnsiClass", instance) != FALSE);

    UINT clipFmtA = RegisterClipboardFormatA("RTRange_0");
    char clipNameA[64]{};
    int clipLenA = GetClipboardFormatNameA(clipFmtA, clipNameA, sizeof(clipNameA));
    Check("VB6_ansi_clipboard_format", clipFmtA >= 0xC000 && clipLenA > 0 && std::strcmp(clipNameA, "RTRange_0") == 0);

    HDC dc = GetDC(nullptr);
    int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    HGDIOBJ guiFont = GetStockObject(DEFAULT_GUI_FONT);
    int released = ReleaseDC(nullptr, dc);
    Check("VB6_headless_gdi_caps_and_font", dc != nullptr && dpiX > 0 && dpiY > 0 &&
        (!shim || (dpiX == 96 && dpiY == 96)) && guiFont != nullptr && released == 1);
    Check("VB6_headless_metrics_and_brush", GetSystemMetrics(SM_CXSCREEN) > 0 &&
        GetSysColorBrush(COLOR_BTNFACE) != nullptr);
    if (sizeof(void*) == 4 && GetFileAttributesW(L"C:\\Windows\\SysWOW64\\msvbvm60.dll") != INVALID_FILE_ATTRIBUTES) {
        HMODULE vb6 = LoadLibraryW(L"msvbvm60.dll");
        if (!vb6) {
            std::printf("msvbvm60 LoadLibrary failed err=%lu\n", GetLastError());
            HMODULE raw = LoadLibraryExW(L"C:\\Windows\\SysWOW64\\msvbvm60.dll", nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
            if (raw) {
                BYTE* base = reinterpret_cast<BYTE*>(reinterpret_cast<ULONG_PTR>(raw) & ~static_cast<ULONG_PTR>(3));
                auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
                auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
                DWORD impRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
                if (impRva) {
                    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + impRva); imp->Name; ++imp) {
                        const char* dll = reinterpret_cast<const char*>(base + imp->Name);
                        HMODULE dep = LoadLibraryA(dll);
                        if (!dep) {
                            std::printf("MISSING DEP DLL: %s err=%lu\n", dll, GetLastError());
                            continue;
                        }
                        DWORD thunkRva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
                        for (auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + thunkRva); thunk->u1.AddressOfData; ++thunk) {
                            if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) {
                                WORD ord = static_cast<WORD>(IMAGE_ORDINAL(thunk->u1.Ordinal));
                                if (!GetProcAddress(dep, reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(ord)))) {
                                    std::printf("MISSING EXPORT: %s!#%u\n", dll, ord);
                                }
                            } else {
                                auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
                                if (!GetProcAddress(dep, reinterpret_cast<const char*>(byName->Name))) {
                                    std::printf("MISSING EXPORT: %s!%s\n", dll, reinterpret_cast<const char*>(byName->Name));
                                }
                            }
                        }
                    }
                }
                FreeLibrary(raw);
            }
        }
        Check("VB6_msvbvm60_load", vb6 != nullptr);
        if (vb6) {
            HMODULE oleaut = GetModuleHandleW(L"oleaut32.dll");
            using LoadRegTypeLibFn = HRESULT(WINAPI*)(REFGUID rguid, WORD wVerMajor, WORD wVerMinor, LCID lcid, ITypeLib** pptlib);
            auto pLoadRegTypeLib = oleaut ? reinterpret_cast<LoadRegTypeLibFn>(GetProcAddress(oleaut, "LoadRegTypeLib")) : nullptr;
            if (pLoadRegTypeLib) {
                static const GUID kVbRunGuid = {0xEA544A21, 0xC82D, 0x11D1, {0xA3, 0xE4, 0x00, 0xA0, 0xC9, 0x0A, 0xEA, 0x82}};
                static const GUID kStdOleGuid = {0x00020430, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
                ITypeLib* tlbVbRun = nullptr;
                ITypeLib* tlbStdOle = nullptr;
                HRESULT hrVbRun = pLoadRegTypeLib(kVbRunGuid, 6, 0, 9, &tlbVbRun);
                HRESULT hrStdOle = pLoadRegTypeLib(kStdOleGuid, 2, 0, 0, &tlbStdOle);
                ITypeInfo* tiUnknown = nullptr;
                HRESULT hrTi = tlbStdOle ? tlbStdOle->GetTypeInfoOfGuid(IID_IUnknown, &tiUnknown) : E_FAIL;
                Check("VB6_typelib_redirect_and_fallback",
                    SUCCEEDED(hrVbRun) && tlbVbRun != nullptr &&
                    SUCCEEDED(hrStdOle) && tlbStdOle != nullptr &&
                    SUCCEEDED(hrTi) && tiUnknown != nullptr);
                if (tiUnknown) tiUnknown->Release();
                if (tlbVbRun) tlbVbRun->Release();
                if (tlbStdOle) tlbStdOle->Release();
            }
            FreeLibrary(vb6);
        }
    }
}

void TestRouting(bool requireShim) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE shim = GetModuleHandleW(L"nanoserver_sta_shim.dll");
    for (const char* name : {"CreateWindowExW", "CreateWindowExA", "SendMessageW", "SendMessageA", "PeekMessageW", "PeekMessageA", "GetClassInfoW", "RegisterClassExA", "CallWindowProcW"}) {
        void* target = EffectiveUser32Target(user32, name);
        HMODULE owner = nullptr;
        wchar_t path[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target), &owner);
        GetModuleFileNameW(owner, path, MAX_PATH);
        const wchar_t* slash = wcsrchr(path, L'\\');
        std::printf("ROUTE %s -> %ls\n", name, slash ? slash + 1 : path);
        Check(name, target && (!requireShim || (shim && owner == shim)));
    }
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    bool requireShim = argc > 1 && std::wcscmp(argv[1], L"--require-shim") == 0;
    runningShim = requireShim;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread watchdog([&] {
        if (WaitForSingleObject(stop, 90000) == WAIT_TIMEOUT) {
            std::fprintf(stderr, "FATAL native regression watchdog expired\n");
            ExitProcess(2);
        }
    });
    std::printf("Native real-import regression bits=%zu requireShim=%d\n", sizeof(void*) * 8, requireShim);
    Require(Register(L"RTBasic") != 0, "register basic");
    TestLifecycleAndExtraBytes();
    TestClasses();
    TestSendOwnership();
    TestQueueLifetimeAndFilteredWait();
    TestTimersWaitsAndQuit();
    TestUserObjectsAndClipboard(requireShim);
    TestAnsiAndHeadlessGdi(requireShim);
    TestRouting(requireShim);
    std::printf("SUMMARY failures=%d\n", failures);
    SetEvent(stop);
    watchdog.join();
    CloseHandle(stop);
    return failures ? 1 : 0;
}
