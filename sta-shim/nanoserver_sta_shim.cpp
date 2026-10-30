// nanoserver_sta_shim.cpp
// Native user-mode Win32 window, message-queue, and COM STA shim for Windows Nano Server 2025.
// Built as both 32-bit (x86) and 64-bit (x64) DLLs with zero external CRT dependencies.
// Can be disabled at process startup or runtime via NANOSERVER_DISABLE_STA_SHIM=1|true|yes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>
#include <bitset>
#include <memory>

extern "C" HRESULT WINAPI CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);

namespace {

constexpr DWORD kErrorModNotFound = 126;
constexpr DWORD kErrorInvalidWindowHandle = 1400;
constexpr int kCachedPresent = 1;
constexpr int kCachedNotPresent = 2;
constexpr UINT kWmQuit = 0x0012;
constexpr UINT kWmTimer = 0x0113;
constexpr int kGwlpWndProc = -4;
constexpr int kGwlpHInstance = -6;
constexpr int kGwlpHwndParent = -8;
constexpr int kGwlpId = -12;
constexpr int kGwlStyle = -16;
constexpr int kGwlExStyle = -20;
constexpr int kGwlpUserData = -21;
constexpr int kUoiName = 2;

const HANDLE kFakeWinSta = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x2001));
const HANDLE kFakeDesktop = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x2002));

using ApiSetQueryApiSetPresenceFn = int(WINAPI*)(LPCVOID namespaceName, PBYTE present);
using ApiSetQueryApiSetPresenceExFn = int(WINAPI*)(LPCVOID namespaceName, PBYTE isInSchema, PBYTE present);
using ResolveDelayLoadedApiFn = PVOID(WINAPI*)(
    PVOID parentModuleBase,
    LPCVOID delayloadDescriptor,
    PVOID failureDllHook,
    PVOID failureSystemHook,
    PVOID* thunkAddress,
    ULONG flags);
using GetModuleHandleWFn = HMODULE(WINAPI*)(LPCWSTR lpModuleName);
using CoInitializeExFn = HRESULT(WINAPI*)(LPVOID pvReserved, DWORD dwCoInit);

struct UnicodeStringHeader {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct DelayLoadDescriptor {
    ULONG Attributes;
    ULONG DllNameRva;
    ULONG ModuleHandleRva;
    ULONG ImportAddressTableRva;
    ULONG ImportNameTableRva;
    ULONG BoundImportAddressTableRva;
    ULONG UnloadInformationTableRva;
    ULONG TimeStamp;
};

struct DelayLoadSlot {
    PVOID* IatSlot;
    PVOID InitialThunk;
    PVOID ShimTarget;
};

struct ModuleHookState {
    HMODULE ModuleBase = nullptr;
    DWORD ImageSize = 0;
    bool HasAppliedState = false;
    bool LastAppliedEnable = false;
    PVOID* PresenceIatSlot = nullptr;
    PVOID* PresenceExIatSlot = nullptr;
    PVOID* ResolveDelayLoadIatSlot = nullptr;
    PVOID* GetModuleHandleWIatSlot = nullptr;
    PVOID OriginalPresenceThunk = nullptr;
    PVOID OriginalPresenceExThunk = nullptr;
    PVOID OriginalResolveDelayLoadThunk = nullptr;
    PVOID OriginalGetModuleHandleWThunk = nullptr;
    std::vector<DelayLoadSlot> ShimmedSlots;
    std::vector<PVOID*> ShimmedHmodAddresses;
    std::vector<int*> KnownPresenceCacheAddresses;
};

struct WindowClassEntry {
    ATOM Atom = 0;
    std::wstring ClassName;
    UINT Style = 0;
    WNDPROC WndProc = nullptr;
    int ClsExtra = 0;
    int WndExtra = 0;
    HINSTANCE HInstance = nullptr;
    HICON Icon = nullptr;
    HICON SmallIcon = nullptr;
    HCURSOR Cursor = nullptr;
    HBRUSH Background = nullptr;
    std::wstring MenuName;
    LPCWSTR MenuResource = nullptr;
};

struct WindowEntry {
    HWND HWnd = nullptr;
    DWORD ThreadId = 0;
    WNDPROC WndProc = nullptr;
    DWORD Style = 0;
    DWORD ExStyle = 0;
    HWND Parent = nullptr;
    HMENU Menu = nullptr;
    HINSTANCE HInstance = nullptr;
    LONG_PTR UserData = 0;
    std::wstring ClassName;
    std::wstring WindowName;
    std::vector<BYTE> ExtraBytes;
    ATOM ClassAtom = 0;
    HINSTANCE ClassInstance = nullptr;
    bool GlobalClass = false;
    bool Destroying = false;
};

struct QueuedMessage {
    HWND HWnd = nullptr;
    UINT Message = 0;
    WPARAM WParam = 0;
    LPARAM LParam = 0;
    DWORD Time = 0;
};

enum class SendState { Pending, Dispatched, Completed, Cancelled };

struct SentMessageItem {
    HWND HWnd = nullptr;
    UINT Message = 0;
    WPARAM WParam = 0;
    LPARAM LParam = 0;
    LRESULT Result = 0;
    HANDLE DoneEvent = nullptr;
    SendState State = SendState::Pending;
    DWORD Error = ERROR_SUCCESS;

    ~SentMessageItem() {
        if (DoneEvent) CloseHandle(DoneEvent);
    }
};

struct TimerEntry {
    HWND HWnd = nullptr;
    UINT_PTR Id = 0;
    UINT Elapse = 0;
    TIMERPROC Callback = nullptr;
    ULONGLONG Deadline = 0;
    bool Ready = false;
};

struct ThreadMessageQueue {
    DWORD ThreadId = 0;
    HANDLE WakeEvent = nullptr;
    LONG InSendMessageDepth = 0;
    bool Exiting = false;
    bool QuitPending = false;
    WPARAM QuitExitCode = 0;
    UINT ChangedMask = 0;
    ULONGLONG ChangeCounter = 0;
    std::vector<QueuedMessage> Posted;
    std::vector<std::shared_ptr<SentMessageItem>> Sent;
    std::vector<TimerEntry> Timers;

    ~ThreadMessageQueue() {
        if (WakeEvent) CloseHandle(WakeEvent);
    }
};

struct ClipboardFormatEntry {
    UINT Id = 0;
    std::wstring Name;
};

CRITICAL_SECTION g_HookLock;
CRITICAL_SECTION g_WindowLock;
INIT_ONCE g_LocksOnce = INIT_ONCE_STATIC_INIT;
DWORD g_QueueFls = FLS_OUT_OF_INDEXES;
bool g_IsActive = false;

std::vector<ModuleHookState> g_HookedModules;
std::vector<WindowClassEntry> g_WindowClasses;
std::vector<WindowEntry> g_Windows;
std::vector<std::shared_ptr<ThreadMessageQueue>> g_ThreadQueues;
std::vector<ClipboardFormatEntry> g_ClipboardFormats;

std::bitset<0x4000> g_ClassAtoms;
UINT g_NextAtom = 0;
LONG g_NextHwnd = 0x10000;
LONG g_NextTimerId = 0x3000;
LONG g_NextClipboardFormat = 0xC000;

ApiSetQueryApiSetPresenceFn g_OrigPresence = nullptr;
ApiSetQueryApiSetPresenceExFn g_OrigPresenceEx = nullptr;
ResolveDelayLoadedApiFn g_OrigResolveDelayLoad = nullptr;
GetModuleHandleWFn g_OrigGetModuleHandleW = nullptr;
CoInitializeExFn g_OrigCoInitializeEx = nullptr;

const char* const kShimmedPrefixes[] = {
    "ext-ms-win-rtcore-ntuser-window-ext-l1-",
    "ext-ms-win-rtcore-ntuser-synch-ext-l1-",
    "ext-ms-win-ntuser-message-l1-",
    "ext-ms-win-ntuser-synch-l1-",
    "ext-ms-win-ntuser-window-l1-",
    "ext-ms-win-ntuser-windowclass-l1-",
    "ext-ms-win-ntuser-windowstation-l1-",
    "ext-ms-win-ntuser-private-l1-",
    "ext-ms-win-ntuser-misc-l1-",
    "ext-ms-win-com-sta-l1-",
};

VOID CALLBACK OnQueueThreadExit(PVOID value);

BOOL CALLBACK InitializeLocks(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_HookLock);
    InitializeCriticalSection(&g_WindowLock);
    g_QueueFls = FlsAlloc(OnQueueThreadExit);
    return TRUE;
}

void EnsureLocksInitialized() {
    InitOnceExecuteOnce(&g_LocksOnce, InitializeLocks, nullptr, nullptr);
}

class ScopedLock {
public:
    explicit ScopedLock(CRITICAL_SECTION& lock) : Lock(lock) {
        EnsureLocksInitialized();
        EnterCriticalSection(&Lock);
    }
    ~ScopedLock() { LeaveCriticalSection(&Lock); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
private:
    CRITICAL_SECTION& Lock;
};

char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

wchar_t ToLowerWide(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

bool EqualsIgnoreCaseWide(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (ToLowerWide(a[i]) != ToLowerWide(b[i])) {
            return false;
        }
    }
    return true;
}

bool StartsWithIgnoreCaseAscii(const char* str, const char* prefix) {
    if (!str || !prefix) {
        return false;
    }
    while (*prefix) {
        if (!*str || ToLowerAscii(*str) != ToLowerAscii(*prefix)) {
            return false;
        }
        ++str;
        ++prefix;
    }
    return true;
}

bool StartsWithIgnoreCaseWideAscii(const wchar_t* wstr, size_t wlen, const char* prefix) {
    if (!wstr || !prefix) {
        return false;
    }
    size_t i = 0;
    while (prefix[i]) {
        if (i >= wlen) {
            return false;
        }
        wchar_t wc = ToLowerWide(wstr[i]);
        char pc = ToLowerAscii(prefix[i]);
        if (wc != static_cast<wchar_t>(static_cast<unsigned char>(pc))) {
            return false;
        }
        ++i;
    }
    return true;
}

bool IsShimmedApiSetAscii(const char* name) {
    if (!name || !*name) {
        return false;
    }
    for (const char* prefix : kShimmedPrefixes) {
        if (StartsWithIgnoreCaseAscii(name, prefix)) {
            return true;
        }
    }
    return false;
}

bool IsShimmedApiSetWide(const wchar_t* name, size_t len) {
    if (!name || len == 0) {
        return false;
    }
    for (const char* prefix : kShimmedPrefixes) {
        if (StartsWithIgnoreCaseWideAscii(name, len, prefix)) {
            return true;
        }
    }
    return false;
}

bool IsShimmedUnicodeString(LPCVOID namespaceName) {
    if (!namespaceName) {
        return false;
    }
    const auto* ustr = reinterpret_cast<const UnicodeStringHeader*>(namespaceName);
    if (!ustr->Buffer || ustr->Length == 0 || ustr->Length > 512) {
        return false;
    }
    return IsShimmedApiSetWide(ustr->Buffer, ustr->Length / sizeof(wchar_t));
}

bool IsOsShimDisabledByEnv() {
    wchar_t buf[64] = {};
    DWORD len = GetEnvironmentVariableW(L"NANOSERVER_DISABLE_STA_SHIM", buf, 63);
    if (len == 0 || len >= 63) {
        return false;
    }
    std::wstring val(buf, len);
    return EqualsIgnoreCaseWide(val, L"1") ||
           EqualsIgnoreCaseWide(val, L"true") ||
           EqualsIgnoreCaseWide(val, L"yes");
}

template<typename T>
bool WriteWithProtection(T* address, T value) {
    ScopedLock lock(g_HookLock);
    MEMORY_BASIC_INFORMATION memory{};
    if (!address || VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        memory.RegionSize < sizeof(T) ||
        reinterpret_cast<ULONG_PTR>(address) - reinterpret_cast<ULONG_PTR>(memory.BaseAddress) >
            memory.RegionSize - sizeof(T)) {
        SetLastError(ERROR_INVALID_ADDRESS);
        return false;
    }
    if (*address == value) return true;
    DWORD protection = memory.Protect & 0xFFu;
    bool executable = protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                      protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    DWORD writable = executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    writable |= memory.Protect & (PAGE_NOCACHE | PAGE_WRITECOMBINE);
    DWORD original = 0;
    if (!VirtualProtect(address, sizeof(T), writable, &original)) return false;
    *address = value;
    DWORD ignored = 0;
    return VirtualProtect(address, sizeof(T), original, &ignored) != FALSE;
}

bool WritePointerWithProtection(PVOID* address, PVOID value) {
    return WriteWithProtection(address, value);
}

bool WriteInt32WithProtection(int* address, int value) {
    return WriteWithProtection(address, value);
}

constexpr UINT kAllQueueBits = 0x1DFFu;
constexpr UINT kPostedBits = QS_POSTMESSAGE | QS_ALLPOSTMESSAGE;

std::shared_ptr<ThreadMessageQueue> FindQueueLocked(DWORD threadId) {
    for (const auto& queue : g_ThreadQueues) {
        if (queue->ThreadId == threadId && !queue->Exiting) return queue;
    }
    return {};
}

void QueueChangedLocked(ThreadMessageQueue& queue, UINT categories = 0) {
    queue.ChangedMask |= categories;
    ++queue.ChangeCounter;
    SetEvent(queue.WakeEvent);
}

UINT MessageCategory(UINT message) {
    UINT bits = kPostedBits;
    if (message >= WM_KEYFIRST && message <= WM_KEYLAST) bits |= QS_KEY;
    if (message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE) bits |= QS_MOUSEMOVE;
    if ((message >= WM_LBUTTONDOWN && message <= WM_MOUSELAST) ||
        (message >= WM_NCLBUTTONDOWN && message <= WM_NCXBUTTONDBLCLK)) bits |= QS_MOUSEBUTTON;
    if (message == WM_TIMER) bits |= QS_TIMER;
    if (message == WM_PAINT) bits |= QS_PAINT;
    if (message == WM_HOTKEY) bits |= QS_HOTKEY;
    if (message == WM_INPUT) bits |= QS_RAWINPUT;
    return bits;
}

WindowEntry* FindWindowLocked(HWND hWnd) {
    for (auto& window : g_Windows) {
        if (window.HWnd == hWnd) return &window;
    }
    return nullptr;
}

void CancelSendLocked(SentMessageItem& item, DWORD error) {
    item.Result = 0;
    item.Error = error;
    item.State = SendState::Cancelled;
    SetEvent(item.DoneEvent);
}

void RemoveWindowLocked(HWND hWnd) {
    g_Windows.erase(std::remove_if(g_Windows.begin(), g_Windows.end(),
        [hWnd](const WindowEntry& window) { return window.HWnd == hWnd; }), g_Windows.end());
    for (const auto& queue : g_ThreadQueues) {
        queue->Timers.erase(std::remove_if(queue->Timers.begin(), queue->Timers.end(),
            [hWnd](const TimerEntry& timer) { return timer.HWnd == hWnd; }), queue->Timers.end());
        queue->Posted.erase(std::remove_if(queue->Posted.begin(), queue->Posted.end(),
            [hWnd](const QueuedMessage& message) { return message.HWnd == hWnd; }), queue->Posted.end());
        for (auto it = queue->Sent.begin(); it != queue->Sent.end();) {
            if ((*it)->HWnd == hWnd && (*it)->State == SendState::Pending) {
                CancelSendLocked(**it, kErrorInvalidWindowHandle);
                it = queue->Sent.erase(it);
            } else {
                ++it;
            }
        }
        QueueChangedLocked(*queue);
    }
}

VOID CALLBACK OnQueueThreadExit(PVOID value) {
    auto* owner = static_cast<std::shared_ptr<ThreadMessageQueue>*>(value);
    if (!owner) return;
    auto queue = *owner;
    {
        ScopedLock lock(g_WindowLock);
        queue->Exiting = true;
        for (const auto& item : queue->Sent) CancelSendLocked(*item, ERROR_INVALID_THREAD_ID);
        queue->Sent.clear();
        queue->Posted.clear();
        queue->Timers.clear();
        g_Windows.erase(std::remove_if(g_Windows.begin(), g_Windows.end(),
            [&queue](const WindowEntry& window) { return window.ThreadId == queue->ThreadId; }), g_Windows.end());
        g_ThreadQueues.erase(std::remove(g_ThreadQueues.begin(), g_ThreadQueues.end(), queue), g_ThreadQueues.end());
        QueueChangedLocked(*queue);
    }
    delete owner;
}

std::shared_ptr<ThreadMessageQueue> GetCurrentThreadQueue() {
    EnsureLocksInitialized();
    if (g_QueueFls == FLS_OUT_OF_INDEXES) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return {};
    }
    auto* owner = static_cast<std::shared_ptr<ThreadMessageQueue>*>(FlsGetValue(g_QueueFls));
    if (owner) return *owner;
    try {
        auto queue = std::make_shared<ThreadMessageQueue>();
        queue->ThreadId = GetCurrentThreadId();
        queue->WakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!queue->WakeEvent) return {};
        auto holder = std::make_unique<std::shared_ptr<ThreadMessageQueue>>(queue);
        ScopedLock lock(g_WindowLock);
        g_ThreadQueues.push_back(queue);
        if (!FlsSetValue(g_QueueFls, holder.get())) {
            g_ThreadQueues.pop_back();
            return {};
        }
        holder.release();
        return queue;
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return {};
    }
}

bool HasPendingSendsLocked(const ThreadMessageQueue& queue) {
    return std::any_of(queue.Sent.begin(), queue.Sent.end(),
        [](const std::shared_ptr<SentMessageItem>& item) { return item->State == SendState::Pending; });
}

WNDPROC GetWndProcForWindow(HWND hWnd) {
    ScopedLock lock(g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    return entry && entry->ThreadId == GetCurrentThreadId() ? entry->WndProc : nullptr;
}

LRESULT InvokeWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WNDPROC proc = GetWndProcForWindow(hWnd);
    return proc ? proc(hWnd, message, wParam, lParam) : 0;
}

void ProcessSentMessages(const std::shared_ptr<ThreadMessageQueue>& queue) {
    for (;;) {
        std::shared_ptr<SentMessageItem> item;
        {
            ScopedLock lock(g_WindowLock);
            if (queue->Exiting) return;
            for (const auto& candidate : queue->Sent) {
                if (candidate->State == SendState::Pending) {
                    item = candidate;
                    break;
                }
            }
            if (!item) return;
            if (!FindWindowLocked(item->HWnd)) {
                CancelSendLocked(*item, kErrorInvalidWindowHandle);
                queue->Sent.erase(std::remove(queue->Sent.begin(), queue->Sent.end(), item), queue->Sent.end());
                continue;
            }
            item->State = SendState::Dispatched;
            ++queue->InSendMessageDepth;
        }
        LRESULT result = InvokeWindowProc(item->HWnd, item->Message, item->WParam, item->LParam);
        {
            ScopedLock lock(g_WindowLock);
            --queue->InSendMessageDepth;
            item->Result = result;
            item->State = SendState::Completed;
            queue->Sent.erase(std::remove(queue->Sent.begin(), queue->Sent.end(), item), queue->Sent.end());
            SetEvent(item->DoneEvent);
        }
    }
}

bool IsChildLocked(HWND parent, HWND child) {
    if (!parent || parent == child || !FindWindowLocked(parent)) return false;
    for (size_t depth = 0; depth < g_Windows.size(); ++depth) {
        WindowEntry* entry = FindWindowLocked(child);
        if (!entry || (entry->Style & WS_CHILD) == 0) return false;
        child = entry->Parent;
        if (child == parent) return true;
        if (!child) return false;
    }
    return false;
}

bool MatchesWindowFilter(HWND messageWindow, HWND filterWindow) {
    if (!filterWindow) return true;
    if (filterWindow == reinterpret_cast<HWND>(static_cast<LONG_PTR>(-1))) return messageWindow == nullptr;
    return messageWindow == filterWindow || IsChildLocked(filterWindow, messageWindow);
}

bool MatchesMessageFilter(UINT message, UINT minimum, UINT maximum) {
    return (minimum == 0 && maximum == 0) || (message >= minimum && message <= maximum);
}

void UpdateTimersLocked(ThreadMessageQueue& queue) {
    if (queue.Timers.empty()) return;
    ULONGLONG now = GetTickCount64();
    for (auto& timer : queue.Timers) {
        if (!timer.Ready && now >= timer.Deadline) {
            timer.Ready = true;
            QueueChangedLocked(queue, QS_TIMER);
        }
    }
}

UINT CurrentQueueBitsLocked(const ThreadMessageQueue& queue) {
    UINT bits = queue.QuitPending ? kPostedBits : 0;
    for (const auto& message : queue.Posted) bits |= MessageCategory(message.Message);
    if (HasPendingSendsLocked(queue)) bits |= QS_SENDMESSAGE;
    for (const auto& timer : queue.Timers) {
        if (timer.Ready) bits |= QS_TIMER;
    }
    return bits;
}

DWORD NextTimerDelayLocked(const ThreadMessageQueue& queue, HWND filterWindow = nullptr,
                          UINT minimum = 0, UINT maximum = 0) {
    DWORD delay = INFINITE;
    if (!MatchesMessageFilter(WM_TIMER, minimum, maximum)) return delay;
    ULONGLONG now = GetTickCount64();
    for (const auto& timer : queue.Timers) {
        if (timer.Ready || !MatchesWindowFilter(timer.HWnd, filterWindow)) continue;
        ULONGLONG remaining = timer.Deadline > now ? timer.Deadline - now : 0;
        if (remaining < delay) delay = static_cast<DWORD>(remaining);
    }
    return delay;
}

bool TryDequeueMessageLocked(ThreadMessageQueue& queue, HWND filterWindow,
                             UINT minimum, UINT maximum, bool remove, QueuedMessage& message) {
    UpdateTimersLocked(queue);
    queue.ChangedMask = 0;
    for (auto it = queue.Posted.begin(); it != queue.Posted.end(); ++it) {
        if (MatchesWindowFilter(it->HWnd, filterWindow) && MatchesMessageFilter(it->Message, minimum, maximum)) {
            message = *it;
            if (remove) queue.Posted.erase(it);
            return true;
        }
    }
    if (queue.QuitPending) {
        message = {nullptr, WM_QUIT, queue.QuitExitCode, 0, GetTickCount()};
        if (remove) queue.QuitPending = false;
        return true;
    }
    if (MatchesMessageFilter(WM_TIMER, minimum, maximum)) {
        for (auto& timer : queue.Timers) {
            if (!timer.Ready || !MatchesWindowFilter(timer.HWnd, filterWindow)) continue;
            message = {timer.HWnd, WM_TIMER, timer.Id, reinterpret_cast<LPARAM>(timer.Callback), GetTickCount()};
            if (remove) {
                timer.Ready = false;
                timer.Deadline = GetTickCount64() + timer.Elapse;
            }
            return true;
        }
    }
    return false;
}

LRESULT WINAPI Shim_DefWindowProcW(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
BOOL WINAPI Shim_DestroyWindow(HWND hWnd);

void DestroyChildWindows(HWND parent) {
    for (;;) {
        HWND child = nullptr;
        {
            ScopedLock lock(g_WindowLock);
            for (const auto& window : g_Windows) {
                if (window.Parent == parent && !window.Destroying && window.ThreadId == GetCurrentThreadId()) {
                    child = window.HWnd;
                    break;
                }
            }
        }
        if (!child) return;
        Shim_DestroyWindow(child);
    }
}

bool IsClassAtom(LPCWSTR name) {
    return (reinterpret_cast<ULONG_PTR>(name) >> 16) == 0;
}

bool ClassMatches(const WindowClassEntry& entry, LPCWSTR name) {
    return name && (IsClassAtom(name) ? entry.Atom == LOWORD(reinterpret_cast<ULONG_PTR>(name))
                                    : EqualsIgnoreCaseWide(entry.ClassName, name));
}

WindowClassEntry* FindClassLocked(LPCWSTR name, HINSTANCE instance) {
    for (auto& entry : g_WindowClasses) {
        if ((entry.Style & CS_GLOBALCLASS) == 0 && entry.HInstance == instance && ClassMatches(entry, name)) return &entry;
    }
    for (auto& entry : g_WindowClasses) {
        if ((entry.Style & CS_GLOBALCLASS) != 0 && ClassMatches(entry, name)) return &entry;
    }
    return nullptr;
}

bool GetSystemClass(LPCWSTR name, WindowClassEntry& entry) {
    if (!name) return false;
    const wchar_t* const names[] = {
        L"Message", L"#32768", L"#32769", L"#32770", L"#32771", L"#32772",
        L"STATIC", L"BUTTON", L"EDIT", L"COMBOBOX", L"LISTBOX", L"SCROLLBAR", L"MDICLIENT"
    };
    std::wstring lookup;
    if (IsClassAtom(name)) {
        wchar_t buffer[24]{};
        swprintf_s(buffer, L"#%u", LOWORD(reinterpret_cast<ULONG_PTR>(name)));
        lookup = buffer;
    } else {
        lookup = name;
    }
    for (const auto* systemName : names) {
        if (EqualsIgnoreCaseWide(lookup, systemName)) {
            entry.ClassName = systemName;
            entry.Style = CS_GLOBALCLASS;
            entry.WndProc = Shim_DefWindowProcW;
            return true;
        }
    }
    return false;
}

ATOM RegisterClassCommon(const WNDCLASSEXW& source) {
    if (!source.lpszClassName || IsClassAtom(source.lpszClassName) || !*source.lpszClassName ||
        !source.lpfnWndProc || source.cbClsExtra < 0 || source.cbWndExtra < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ScopedLock lock(g_WindowLock);
    bool global = (source.style & CS_GLOBALCLASS) != 0;
    HINSTANCE instance = source.hInstance ? source.hInstance : GetModuleHandleW(nullptr);
    ATOM sharedAtom = 0;
    for (const auto& entry : g_WindowClasses) {
        if (EqualsIgnoreCaseWide(entry.ClassName, source.lpszClassName)) {
            sharedAtom = entry.Atom;
            if (((entry.Style & CS_GLOBALCLASS) != 0) == global && (global || entry.HInstance == instance)) {
                SetLastError(ERROR_CLASS_ALREADY_EXISTS);
                return 0;
            }
        }
    }
    UINT index = sharedAtom ? sharedAtom - 0xC000u : 0;
    if (!sharedAtom) {
        for (; index < 0x4000; ++index) {
            if (!g_ClassAtoms.test((g_NextAtom + index) & 0x3FFFu)) break;
        }
        if (index == 0x4000) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        index = (g_NextAtom + index) & 0x3FFFu;
    }
    WindowClassEntry entry;
    entry.Atom = static_cast<ATOM>(0xC000u + index);
    entry.ClassName = source.lpszClassName;
    entry.Style = source.style;
    entry.WndProc = source.lpfnWndProc;
    entry.ClsExtra = source.cbClsExtra;
    entry.WndExtra = source.cbWndExtra;
    entry.HInstance = instance;
    entry.Icon = source.hIcon;
    entry.SmallIcon = source.hIconSm;
    entry.Cursor = source.hCursor;
    entry.Background = source.hbrBackground;
    if (source.lpszMenuName && !IsClassAtom(source.lpszMenuName)) entry.MenuName = source.lpszMenuName;
    else entry.MenuResource = source.lpszMenuName;
    g_WindowClasses.push_back(std::move(entry));
    g_ClassAtoms.set(index);
    if (!sharedAtom) g_NextAtom = (index + 1) & 0x3FFFu;
    return static_cast<ATOM>(0xC000u + index);
}

ATOM WINAPI Shim_RegisterClassW(const WNDCLASSW* source) {
    if (!source) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WNDCLASSEXW extended{};
    extended.cbSize = sizeof(extended);
    extended.style = source->style;
    extended.lpfnWndProc = source->lpfnWndProc;
    extended.cbClsExtra = source->cbClsExtra;
    extended.cbWndExtra = source->cbWndExtra;
    extended.hInstance = source->hInstance;
    extended.hIcon = source->hIcon;
    extended.hCursor = source->hCursor;
    extended.hbrBackground = source->hbrBackground;
    extended.lpszMenuName = source->lpszMenuName;
    extended.lpszClassName = source->lpszClassName;
    return RegisterClassCommon(extended);
}

ATOM WINAPI Shim_RegisterClassExW(const WNDCLASSEXW* source) {
    if (!source || source->cbSize != sizeof(WNDCLASSEXW)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return RegisterClassCommon(*source);
}

BOOL WINAPI Shim_UnregisterClassW(LPCWSTR name, HINSTANCE instance) {
    ScopedLock lock(g_WindowLock);
    WindowClassEntry* entry = FindClassLocked(name, instance);
    if (!entry) {
        SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
        return FALSE;
    }
    ATOM atom = entry->Atom;
    HINSTANCE classInstance = entry->HInstance;
    bool global = (entry->Style & CS_GLOBALCLASS) != 0;
    for (const auto& window : g_Windows) {
        if (window.ClassAtom == atom && window.ClassInstance == classInstance && window.GlobalClass == global) {
            SetLastError(ERROR_CLASS_HAS_WINDOWS);
            return FALSE;
        }
    }
    g_WindowClasses.erase(std::remove_if(g_WindowClasses.begin(), g_WindowClasses.end(),
        [atom, classInstance, global](const WindowClassEntry& item) {
            return item.Atom == atom && item.HInstance == classInstance &&
                ((item.Style & CS_GLOBALCLASS) != 0) == global;
        }), g_WindowClasses.end());
    if (std::none_of(g_WindowClasses.begin(), g_WindowClasses.end(),
        [atom](const WindowClassEntry& item) { return item.Atom == atom; })) g_ClassAtoms.reset(atom - 0xC000u);
    return TRUE;
}

extern "C" BOOL WINAPI Shim_GetClassInfoExW(HINSTANCE instance, LPCWSTR name, WNDCLASSEXW* output) {
    if (!output || output->cbSize != sizeof(WNDCLASSEXW)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ScopedLock lock(g_WindowLock);
    WindowClassEntry system;
    WindowClassEntry* entry = FindClassLocked(name, instance);
    if (!entry && GetSystemClass(name, system)) entry = &system;
    if (!entry) {
        SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
        return FALSE;
    }
    output->style = entry->Style;
    output->lpfnWndProc = entry->WndProc;
    output->cbClsExtra = entry->ClsExtra;
    output->cbWndExtra = entry->WndExtra;
    output->hInstance = entry->HInstance;
    output->hIcon = entry->Icon;
    output->hIconSm = entry->SmallIcon;
    output->hCursor = entry->Cursor;
    output->hbrBackground = entry->Background;
    output->lpszMenuName = entry->MenuName.empty() ? entry->MenuResource : entry->MenuName.c_str();
    output->lpszClassName = name;
    // Combase reuses the successful query result as a class atom.
    return entry->Atom ? entry->Atom : TRUE;
}

extern "C" BOOL WINAPI Shim_GetClassInfoW(HINSTANCE instance, LPCWSTR name, WNDCLASSW* output) {
    if (!output) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WNDCLASSEXW extended{};
    extended.cbSize = sizeof(extended);
    BOOL atom = Shim_GetClassInfoExW(instance, name, &extended);
    if (!atom) return FALSE;
    output->style = extended.style;
    output->lpfnWndProc = extended.lpfnWndProc;
    output->cbClsExtra = extended.cbClsExtra;
    output->cbWndExtra = extended.cbWndExtra;
    output->hInstance = extended.hInstance;
    output->hIcon = extended.hIcon;
    output->hCursor = extended.hCursor;
    output->hbrBackground = extended.hbrBackground;
    output->lpszMenuName = extended.lpszMenuName;
    output->lpszClassName = extended.lpszClassName;
    return atom;
}

extern "C" LRESULT WINAPI Shim_CallWindowProcW(WNDPROC previous, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return previous ? previous(hWnd, message, wParam, lParam) : 0;
}

LRESULT WINAPI Shim_DefWindowProcW(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    (void)lParam;
    if (message == WM_NCCREATE) return TRUE;
    if (message == WM_CLOSE) Shim_DestroyWindow(hWnd);
    return 0;
}

HWND WINAPI Shim_CreateWindowExW(
    DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y, int width, int height,
    HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    auto queue = GetCurrentThreadQueue();
    if (!queue) return nullptr;
    HWND hWnd = nullptr;
    {
        ScopedLock lock(g_WindowLock);
        WindowClassEntry system;
        WindowClassEntry* cls = nullptr;
        // Creation with a null instance searches local classes in reverse registration
        // order. Class queries still require the defining instance.
        if (!instance) {
            for (auto it = g_WindowClasses.rbegin(); it != g_WindowClasses.rend(); ++it) {
                if ((it->Style & CS_GLOBALCLASS) == 0 && ClassMatches(*it, className)) {
                    cls = &*it;
                    break;
                }
            }
        }
        if (!cls) cls = FindClassLocked(className, instance);
        if (!cls && GetSystemClass(className, system)) cls = &system;
        if (!cls) {
            SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
            return nullptr;
        }
        if (parent && parent != HWND_MESSAGE) {
            WindowEntry* parentEntry = FindWindowLocked(parent);
            if (!parentEntry) {
                SetLastError(kErrorInvalidWindowHandle);
                return nullptr;
            }
            if ((style & WS_CHILD) != 0 && parentEntry->ThreadId != queue->ThreadId) {
                SetLastError(ERROR_ACCESS_DENIED);
                return nullptr;
            }
        }
        WindowEntry entry;
        entry.HWnd = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(InterlockedIncrement(&g_NextHwnd)));
        entry.ThreadId = queue->ThreadId;
        entry.WndProc = cls->WndProc;
        entry.Style = style;
        entry.ExStyle = exStyle;
        entry.Parent = parent;
        entry.Menu = menu;
        entry.HInstance = instance;
        entry.ClassName = cls->ClassName;
        entry.ClassAtom = cls->Atom;
        entry.ClassInstance = cls->HInstance;
        entry.GlobalClass = (cls->Style & CS_GLOBALCLASS) != 0;
        entry.WindowName = windowName ? windowName : L"";
        entry.ExtraBytes.resize(static_cast<size_t>(cls->WndExtra), 0);
        hWnd = entry.HWnd;
        g_Windows.push_back(std::move(entry));
    }
    CREATESTRUCTW creation{};
    creation.lpCreateParams = param;
    creation.hInstance = instance;
    creation.hMenu = menu;
    creation.hwndParent = parent;
    creation.cy = height;
    creation.cx = width;
    creation.y = y;
    creation.x = x;
    creation.style = static_cast<LONG>(style);
    creation.lpszName = windowName;
    creation.lpszClass = className;
    creation.dwExStyle = exStyle;
    if (!InvokeWindowProc(hWnd, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&creation))) {
        {
            ScopedLock lock(g_WindowLock);
            WindowEntry* entry = FindWindowLocked(hWnd);
            if (entry) entry->Destroying = true;
        }
        DestroyChildWindows(hWnd);
        InvokeWindowProc(hWnd, WM_NCDESTROY, 0, 0);
        ScopedLock lock(g_WindowLock);
        RemoveWindowLocked(hWnd);
        return nullptr;
    }
    if (!GetWndProcForWindow(hWnd)) return nullptr;
    if (InvokeWindowProc(hWnd, WM_CREATE, 0, reinterpret_cast<LPARAM>(&creation)) == -1) {
        Shim_DestroyWindow(hWnd);
        return nullptr;
    }
    return GetWndProcForWindow(hWnd) ? hWnd : nullptr;
}

BOOL WINAPI Shim_DestroyWindow(HWND hWnd) {
    {
        ScopedLock lock(g_WindowLock);
        WindowEntry* entry = FindWindowLocked(hWnd);
        if (!entry) {
            SetLastError(kErrorInvalidWindowHandle);
            return FALSE;
        }
        if (entry->ThreadId != GetCurrentThreadId()) {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        if (entry->Destroying) return TRUE;
        entry->Destroying = true;
    }
    InvokeWindowProc(hWnd, WM_DESTROY, 0, 0);
    DestroyChildWindows(hWnd);
    InvokeWindowProc(hWnd, WM_NCDESTROY, 0, 0);
    ScopedLock lock(g_WindowLock);
    RemoveWindowLocked(hWnd);
    return TRUE;
}

BOOL WINAPI Shim_IsWindow(HWND hWnd) {
    ScopedLock lock(g_WindowLock);
    return FindWindowLocked(hWnd) ? TRUE : FALSE;
}

BOOL WINAPI Shim_IsChild(HWND parent, HWND child) {
    ScopedLock lock(g_WindowLock);
    return IsChildLocked(parent, child) ? TRUE : FALSE;
}

template<typename T>
T WindowLongCommon(HWND hWnd, int index, bool write, T value = 0) {
    ScopedLock lock(g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    if (!entry) {
        SetLastError(kErrorInvalidWindowHandle);
        return 0;
    }
    if (index >= 0) {
        size_t offset = static_cast<size_t>(index);
        if (offset > entry->ExtraBytes.size() || sizeof(T) > entry->ExtraBytes.size() - offset) {
            SetLastError(ERROR_INVALID_INDEX);
            return 0;
        }
        T previous = 0;
        memcpy(&previous, entry->ExtraBytes.data() + offset, sizeof(T));
        if (write) memcpy(entry->ExtraBytes.data() + offset, &value, sizeof(T));
        return previous;
    }
    LONG_PTR previous = 0;
    switch (index) {
        case kGwlpWndProc:
            previous = reinterpret_cast<LONG_PTR>(entry->WndProc);
            if (write) entry->WndProc = reinterpret_cast<WNDPROC>(static_cast<LONG_PTR>(value));
            break;
        case kGwlpHInstance:
            previous = reinterpret_cast<LONG_PTR>(entry->HInstance);
            if (write) entry->HInstance = reinterpret_cast<HINSTANCE>(static_cast<LONG_PTR>(value));
            break;
        case kGwlpHwndParent:
            previous = reinterpret_cast<LONG_PTR>(entry->Parent);
            if (write) entry->Parent = reinterpret_cast<HWND>(static_cast<LONG_PTR>(value));
            break;
        case kGwlpId:
            previous = reinterpret_cast<LONG_PTR>(entry->Menu);
            if (write) entry->Menu = reinterpret_cast<HMENU>(static_cast<LONG_PTR>(value));
            break;
        case kGwlStyle:
            previous = entry->Style;
            if (write) entry->Style = static_cast<DWORD>(value);
            break;
        case kGwlExStyle:
            previous = entry->ExStyle;
            if (write) entry->ExStyle = static_cast<DWORD>(value);
            break;
        case kGwlpUserData:
            previous = entry->UserData;
            if (write) entry->UserData = static_cast<LONG_PTR>(value);
            break;
        default:
            SetLastError(ERROR_INVALID_INDEX);
            return 0;
    }
    return static_cast<T>(previous);
}

LONG_PTR WINAPI Shim_GetWindowLongPtrW(HWND hWnd, int index) {
    return WindowLongCommon<LONG_PTR>(hWnd, index, false);
}

LONG_PTR WINAPI Shim_SetWindowLongPtrW(HWND hWnd, int index, LONG_PTR value) {
    return WindowLongCommon<LONG_PTR>(hWnd, index, true, value);
}

LONG WINAPI Shim_GetWindowLongW(HWND hWnd, int index) {
    return WindowLongCommon<LONG>(hWnd, index, false);
}

LONG WINAPI Shim_SetWindowLongW(HWND hWnd, int index, LONG value) {
    return WindowLongCommon<LONG>(hWnd, index, true, value);
}

DWORD WINAPI Shim_GetWindowThreadProcessId(HWND hWnd, LPDWORD processId) {
    ScopedLock lock(g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    if (!entry) {
        SetLastError(kErrorInvalidWindowHandle);
        return 0;
    }
    if (processId) *processId = GetCurrentProcessId();
    return entry->ThreadId;
}

void WriteMsgStruct(LPMSG output, const QueuedMessage& message) {
    *output = {};
    output->hwnd = message.HWnd;
    output->message = message.Message;
    output->wParam = message.WParam;
    output->lParam = message.LParam;
    output->time = message.Time;
}

bool ValidateMessageArguments(LPMSG output, HWND filterWindow, UINT minimum, UINT maximum) {
    if (!output || ((minimum != 0 || maximum != 0) && minimum > maximum)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (filterWindow && filterWindow != reinterpret_cast<HWND>(static_cast<LONG_PTR>(-1))) {
        ScopedLock lock(g_WindowLock);
        if (!FindWindowLocked(filterWindow)) {
            SetLastError(kErrorInvalidWindowHandle);
            return false;
        }
    }
    return true;
}

BOOL WINAPI Shim_PeekMessageW(LPMSG output, HWND filterWindow, UINT minimum, UINT maximum, UINT removeFlags) {
    if (!ValidateMessageArguments(output, filterWindow, minimum, maximum)) return FALSE;
    auto queue = GetCurrentThreadQueue();
    if (!queue) return FALSE;
    ProcessSentMessages(queue);
    QueuedMessage message;
    ScopedLock lock(g_WindowLock);
    if (!TryDequeueMessageLocked(*queue, filterWindow, minimum, maximum, (removeFlags & PM_REMOVE) != 0, message)) return FALSE;
    WriteMsgStruct(output, message);
    return TRUE;
}

BOOL WINAPI Shim_GetMessageW(LPMSG output, HWND filterWindow, UINT minimum, UINT maximum) {
    if (!ValidateMessageArguments(output, filterWindow, minimum, maximum)) return -1;
    auto queue = GetCurrentThreadQueue();
    if (!queue) return -1;
    for (;;) {
        ProcessSentMessages(queue);
        ULONGLONG observed = 0;
        {
            ScopedLock lock(g_WindowLock);
            QueuedMessage message;
            if (TryDequeueMessageLocked(*queue, filterWindow, minimum, maximum, true, message)) {
                WriteMsgStruct(output, message);
                return message.Message == WM_QUIT ? FALSE : TRUE;
            }
            observed = queue->ChangeCounter;
        }
        DWORD delay = INFINITE;
        {
            ScopedLock lock(g_WindowLock);
            if (queue->ChangeCounter != observed || HasPendingSendsLocked(*queue)) continue;
            ResetEvent(queue->WakeEvent);
            delay = NextTimerDelayLocked(*queue, filterWindow, minimum, maximum);
        }
        if (WaitForSingleObject(queue->WakeEvent, delay) == WAIT_FAILED) return -1;
    }
}

BOOL WINAPI Shim_TranslateMessage(const MSG* message) {
    (void)message;
    return FALSE;
}

LRESULT WINAPI Shim_DispatchMessageW(const MSG* message) {
    if (!message) return 0;
    if (message->message == WM_TIMER && message->lParam != 0) {
        auto callback = reinterpret_cast<TIMERPROC>(message->lParam);
        callback(message->hwnd, WM_TIMER, static_cast<UINT_PTR>(message->wParam), GetTickCount());
        return 0;
    }
    return InvokeWindowProc(message->hwnd, message->message, message->wParam, message->lParam);
}

void PostToQueueLocked(ThreadMessageQueue& queue, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_QUIT) {
        queue.QuitPending = true;
        queue.QuitExitCode = wParam;
    } else {
        queue.Posted.push_back({hWnd, message, wParam, lParam, GetTickCount()});
    }
    QueueChangedLocked(queue, MessageCategory(message));
}

BOOL WINAPI Shim_PostMessageW(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (!hWnd && !GetCurrentThreadQueue()) return FALSE;
    ScopedLock lock(g_WindowLock);
    DWORD targetId = GetCurrentThreadId();
    if (hWnd) {
        WindowEntry* window = FindWindowLocked(hWnd);
        if (!window) {
            SetLastError(kErrorInvalidWindowHandle);
            return FALSE;
        }
        targetId = window->ThreadId;
    }
    auto queue = FindQueueLocked(targetId);
    if (!queue) {
        SetLastError(ERROR_INVALID_THREAD_ID);
        return FALSE;
    }
    PostToQueueLocked(*queue, hWnd, message, wParam, lParam);
    return TRUE;
}

BOOL WINAPI Shim_PostThreadMessageW(DWORD threadId, UINT message, WPARAM wParam, LPARAM lParam) {
    if (threadId == GetCurrentThreadId() && !GetCurrentThreadQueue()) return FALSE;
    ScopedLock lock(g_WindowLock);
    auto queue = FindQueueLocked(threadId);
    if (!queue) {
        SetLastError(ERROR_INVALID_THREAD_ID);
        return FALSE;
    }
    PostToQueueLocked(*queue, nullptr, message, wParam, lParam);
    return TRUE;
}

void WINAPI Shim_PostQuitMessage(int exitCode) {
    Shim_PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, static_cast<WPARAM>(exitCode), 0);
}

LRESULT WINAPI Shim_SendMessageW(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto current = GetCurrentThreadQueue();
    if (!current) return 0;
    auto item = std::make_shared<SentMessageItem>();
    item->HWnd = hWnd;
    item->Message = message;
    item->WParam = wParam;
    item->LParam = lParam;
    std::shared_ptr<ThreadMessageQueue> target;
    bool sameThread = false;
    {
        ScopedLock lock(g_WindowLock);
        WindowEntry* window = FindWindowLocked(hWnd);
        if (!window) {
            SetLastError(kErrorInvalidWindowHandle);
            return 0;
        }
        sameThread = window->ThreadId == current->ThreadId;
        if (!sameThread) {
            target = FindQueueLocked(window->ThreadId);
            if (!target) {
                SetLastError(ERROR_INVALID_THREAD_ID);
                return 0;
            }
            item->DoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!item->DoneEvent) return 0;
            target->Sent.push_back(item);
            QueueChangedLocked(*target, QS_SENDMESSAGE);
        }
    }
    if (sameThread) return InvokeWindowProc(hWnd, message, wParam, lParam);
    HANDLE handles[] = {item->DoneEvent, current->WakeEvent};
    for (;;) {
        ProcessSentMessages(current);
        {
            ScopedLock lock(g_WindowLock);
            if (item->State == SendState::Completed || item->State == SendState::Cancelled) {
                if (item->Error) SetLastError(item->Error);
                return item->Result;
            }
            if (HasPendingSendsLocked(*current)) continue;
            ResetEvent(current->WakeEvent);
        }
        // No timeout: the receiver may still be using data in the sender's frame.
        WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    }
}

DWORD WINAPI Shim_GetQueueStatus(UINT flags) {
    if ((flags & ~kAllQueueBits) != 0) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    auto queue = GetCurrentThreadQueue();
    if (!queue) return 0;
    ScopedLock lock(g_WindowLock);
    UpdateTimersLocked(*queue);
    UINT changed = queue->ChangedMask & flags;
    UINT current = CurrentQueueBitsLocked(*queue) & flags;
    queue->ChangedMask &= ~flags;
    return MAKELONG(changed, current);
}

DWORD WINAPI Shim_InSendMessageEx(LPVOID reserved) {
    (void)reserved;
    auto queue = GetCurrentThreadQueue();
    if (!queue) return 0;
    ScopedLock lock(g_WindowLock);
    return queue->InSendMessageDepth > 0 ? ISMEX_SEND : ISMEX_NOSEND;
}

BOOL WINAPI Shim_IsGUIThread(BOOL convert) {
    EnsureLocksInitialized();
    if (convert) return GetCurrentThreadQueue() ? TRUE : FALSE;
    return g_QueueFls != FLS_OUT_OF_INDEXES && FlsGetValue(g_QueueFls) ? TRUE : FALSE;
}

BOOL WINAPI Shim_AllowSetForegroundWindow(DWORD processId) {
    (void)processId;
    return TRUE;
}

UINT_PTR WINAPI Shim_SetTimer(HWND hWnd, UINT_PTR timerId, UINT elapse, TIMERPROC callback) {
    auto queue = GetCurrentThreadQueue();
    if (!queue) return 0;
    ScopedLock lock(g_WindowLock);
    if (hWnd) {
        WindowEntry* window = FindWindowLocked(hWnd);
        if (!window) {
            SetLastError(kErrorInvalidWindowHandle);
            return 0;
        }
        if (window->ThreadId != queue->ThreadId) {
            SetLastError(ERROR_ACCESS_DENIED);
            return 0;
        }
    }
    elapse = (std::max)(10u, (std::min)(elapse, 0x7FFFFFFFu));
    for (auto& timer : queue->Timers) {
        if (timer.HWnd == hWnd && timer.Id == timerId && timerId != 0) {
            timer.Elapse = elapse;
            timer.Callback = callback;
            timer.Deadline = GetTickCount64() + elapse;
            timer.Ready = false;
            QueueChangedLocked(*queue);
            return timer.Id;
        }
    }
    if (!timerId || !hWnd) {
        do {
            timerId = static_cast<UINT_PTR>(static_cast<ULONG>(InterlockedIncrement(&g_NextTimerId)));
        } while (!timerId || std::any_of(queue->Timers.begin(), queue->Timers.end(),
            [timerId](const TimerEntry& timer) { return timer.Id == timerId; }));
    }
    queue->Timers.push_back({hWnd, timerId, elapse, callback, GetTickCount64() + elapse, false});
    QueueChangedLocked(*queue);
    return timerId;
}

UINT_PTR WINAPI Shim_SetCoalescableTimer(HWND hWnd, UINT_PTR timerId, UINT elapse, TIMERPROC callback, ULONG tolerance) {
    (void)tolerance;
    return Shim_SetTimer(hWnd, timerId, elapse, callback);
}

BOOL WINAPI Shim_KillTimer(HWND hWnd, UINT_PTR timerId) {
    auto queue = GetCurrentThreadQueue();
    if (!queue) return FALSE;
    ScopedLock lock(g_WindowLock);
    if (hWnd && !FindWindowLocked(hWnd)) {
        SetLastError(kErrorInvalidWindowHandle);
        return FALSE;
    }
    for (auto it = queue->Timers.begin(); it != queue->Timers.end(); ++it) {
        if (it->HWnd == hWnd && it->Id == timerId) {
            queue->Timers.erase(it);
            QueueChangedLocked(*queue);
            return TRUE;
        }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

DWORD WINAPI Shim_MsgWaitForMultipleObjectsEx(DWORD count, const HANDLE* inputHandles,
                                             DWORD milliseconds, DWORD wakeMask, DWORD flags) {
    if (count > MAXIMUM_WAIT_OBJECTS - 1 || (count && !inputHandles) || (flags & ~0x0007u) != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    auto queue = GetCurrentThreadQueue();
    if (!queue) return WAIT_FAILED;
    HANDLE handles[MAXIMUM_WAIT_OBJECTS]{};
    for (DWORD i = 0; i < count; ++i) handles[i] = inputHandles[i];
    handles[count] = queue->WakeEvent;
    bool waitAll = (flags & MWMO_WAITALL) != 0;
    bool inputAvailable = (flags & MWMO_INPUTAVAILABLE) != 0;
    BOOL alertable = (flags & MWMO_ALERTABLE) != 0;
    ULONGLONG started = GetTickCount64();
    bool observedInput = false;
    for (;;) {
        {
            ScopedLock lock(g_WindowLock);
            UpdateTimersLocked(*queue);
            UINT status = inputAvailable ? CurrentQueueBitsLocked(*queue) : queue->ChangedMask;
            observedInput = observedInput || (status & wakeMask) != 0;
            queue->ChangedMask &= ~wakeMask;
        }
        ProcessSentMessages(queue);
        DWORD timerDelay = INFINITE;
        {
            ScopedLock lock(g_WindowLock);
            UpdateTimersLocked(*queue);
            UINT status = inputAvailable ? CurrentQueueBitsLocked(*queue) : queue->ChangedMask;
            observedInput = observedInput || (status & wakeMask) != 0;
            queue->ChangedMask &= ~wakeMask;
            if (HasPendingSendsLocked(*queue)) continue;
            ResetEvent(queue->WakeEvent);
            if ((wakeMask & QS_TIMER) != 0) timerDelay = NextTimerDelayLocked(*queue);
        }
        if (observedInput && (!waitAll || count == 0)) {
            if (count) {
                DWORD result = WaitForMultipleObjectsEx(count, handles, FALSE, 0, alertable);
                if (result != WAIT_TIMEOUT) return result;
            }
            return WAIT_OBJECT_0 + count;
        }
        ULONGLONG elapsed = GetTickCount64() - started;
        DWORD remaining = milliseconds == INFINITE ? INFINITE :
            (elapsed >= milliseconds ? 0 : milliseconds - static_cast<DWORD>(elapsed));
        DWORD delay = (std::min)(remaining, timerDelay);
        DWORD result = WAIT_TIMEOUT;
        if (waitAll && count > 0 && observedInput) {
            // Check all handles atomically, without consuming auto-reset handles individually.
            // Short bounded waits let this thread continue servicing incoming synchronous sends.
            result = WaitForMultipleObjectsEx(count, handles, TRUE, (std::min)(delay, DWORD{20}), alertable);
        } else if (waitAll && count > 0) {
            result = WaitForSingleObjectEx(queue->WakeEvent, delay, alertable);
            if (result == WAIT_OBJECT_0) continue;
        } else {
            result = WaitForMultipleObjectsEx(count + 1, handles, FALSE, delay, alertable);
            if (result == WAIT_OBJECT_0 + count) continue;
            if (result != WAIT_TIMEOUT) return result;
        }
        if (result != WAIT_TIMEOUT) return result;
        if (milliseconds != INFINITE && GetTickCount64() - started >= milliseconds) return WAIT_TIMEOUT;
    }
}

BOOL WINAPI Shim_IsThreadMessageQueueAttached() {
    return Shim_IsGUIThread(FALSE);
}

BOOL WINAPI Shim_SetThreadInputBlocked(BOOL blocked) {
    (void)blocked;
    return TRUE;
}

HANDLE WINAPI Shim_GetProcessWindowStation() {
    return kFakeWinSta;
}

HANDLE WINAPI Shim_GetThreadDesktop(DWORD threadId) {
    (void)threadId;
    return kFakeDesktop;
}

BOOL WINAPI Shim_GetUserObjectInformationW(HANDLE object, int index, PVOID output, DWORD length, LPDWORD needed) {
    if (object != kFakeWinSta && object != kFakeDesktop) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    USEROBJECTFLAGS objectFlags{}; // Matches a non-interactive Server Core container.
    const wchar_t* name = object == kFakeWinSta ? L"WinSta0" : L"Default";
    const void* source = nullptr;
    DWORD required = 0;
    if (index == UOI_FLAGS) {
        source = &objectFlags;
        required = sizeof(objectFlags);
    } else if (index == kUoiName) {
        source = name;
        required = static_cast<DWORD>((wcslen(name) + 1) * sizeof(wchar_t));
    } else {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (needed) *needed = required;
    if (!output || length < required) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(output, source, required);
    return TRUE;
}

UINT WINAPI Shim_RegisterClipboardFormatW(LPCWSTR name) {
    if (!name || !*name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ScopedLock lock(g_WindowLock);
    for (const auto& entry : g_ClipboardFormats) {
        if (EqualsIgnoreCaseWide(entry.Name, name)) return entry.Id;
    }
    if (g_NextClipboardFormat > 0xFFFF) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    UINT id = static_cast<UINT>(g_NextClipboardFormat);
    g_ClipboardFormats.push_back({id, name});
    ++g_NextClipboardFormat;
    return id;
}

int WINAPI Shim_GetClipboardFormatNameW(UINT format, LPWSTR output, int capacity) {
    if (!output || capacity <= 0) return 0;
    ScopedLock lock(g_WindowLock);
    for (const auto& entry : g_ClipboardFormats) {
        if (entry.Id == format) {
            int count = (std::min)(static_cast<int>(entry.Name.size()), capacity - 1);
            memcpy(output, entry.Name.data(), static_cast<size_t>(count) * sizeof(wchar_t));
            output[count] = L'\0';
            return count;
        }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

BOOL WINAPI Shim_MessageBeep(UINT type) {
    (void)type;
    return TRUE;
}

HRESULT WINAPI Shim_CoInitialize(LPVOID reserved) {
    return CoInitializeEx(reserved, 0x2 /* COINIT_APARTMENTTHREADED */);
}

extern "C" HANDLE WINAPI Shim_CopyEnhMetaFileA(HANDLE hemfSrc, LPCSTR lpszFile) {
    (void)lpszFile;
    return hemfSrc ? hemfSrc : reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x53484D01u));
}

extern "C" HANDLE WINAPI Shim_CreatePenIndirect(const void* plpen) {
    (void)plpen;
    return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x53484D02u));
}

extern "C" BOOL WINAPI Shim_GetTextExtentPoint32A(HANDLE hdc, LPCSTR lpString, int c, LPSIZE psizl) {
    (void)hdc;
    if (!psizl) {
        return FALSE;
    }
    if (c < 0) {
        c = lpString ? static_cast<int>(strlen(lpString)) : 0;
    }
    psizl->cx = (c > 0) ? (c * 8) : 0;
    psizl->cy = 16;
    return TRUE;
}

extern "C" HANDLE WINAPI Shim_SetWinMetaFileBits(
    UINT nSize,
    const BYTE* lpMeta16Data,
    HANDLE hdcRef,
    const void* lpMFP) {
    (void)nSize;
    (void)lpMeta16Data;
    (void)hdcRef;
    (void)lpMFP;
    return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x53484D03u));
}

extern "C" HANDLE WINAPI Shim_CreateIcon(
    HINSTANCE hInstance,
    int nWidth,
    int nHeight,
    BYTE cPlanes,
    BYTE cBitsPixel,
    const BYTE* lpbANDbits,
    const BYTE* lpbXORbits) {
    (void)hInstance;
    (void)nWidth;
    (void)nHeight;
    (void)cPlanes;
    (void)cBitsPixel;
    (void)lpbANDbits;
    (void)lpbXORbits;
    return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x53484D04u));
}

extern "C" int WINAPI Shim_GetKeyNameTextA(LONG lParam, LPSTR lpString, int cchSize) {
    (void)lParam;
    if (!lpString || cchSize <= 0) {
        return 0;
    }
    const char* kDefaultKeyName = "Key";
    int copyLen = 3;
    if (copyLen >= cchSize) {
        copyLen = cchSize - 1;
    }
    if (copyLen > 0) {
        memcpy(lpString, kDefaultKeyName, static_cast<size_t>(copyLen));
    }
    lpString[copyLen] = '\0';
    return copyLen;
}

extern "C" BOOL WINAPI Shim_ImageList_GetIconSize(PVOID himl, int* cx, int* cy) {
    (void)himl;
    if (cx) {
        *cx = 16;
    }
    if (cy) {
        *cy = 16;
    }
    return TRUE;
}

extern "C" BOOL WINAPI Shim_ImageList_SetIconSize(PVOID himl, int cx, int cy) {
    (void)himl;
    (void)cx;
    (void)cy;
    return TRUE;
}

struct ExportMapping {
    const char* Name;
    PVOID Target;
};

const ExportMapping kExportTable[] = {
    {"RegisterClassW", reinterpret_cast<PVOID>(Shim_RegisterClassW)},
    {"RegisterClassExW", reinterpret_cast<PVOID>(Shim_RegisterClassExW)},
    {"UnregisterClassW", reinterpret_cast<PVOID>(Shim_UnregisterClassW)},
    {"GetClassInfoW", reinterpret_cast<PVOID>(Shim_GetClassInfoW)},
    {"GetClassInfoExW", reinterpret_cast<PVOID>(Shim_GetClassInfoExW)},
    {"CallWindowProcW", reinterpret_cast<PVOID>(Shim_CallWindowProcW)},
    {"CreateWindowExW", reinterpret_cast<PVOID>(Shim_CreateWindowExW)},
    {"DestroyWindow", reinterpret_cast<PVOID>(Shim_DestroyWindow)},
    {"DefWindowProcW", reinterpret_cast<PVOID>(Shim_DefWindowProcW)},
    {"IsWindow", reinterpret_cast<PVOID>(Shim_IsWindow)},
    {"IsChild", reinterpret_cast<PVOID>(Shim_IsChild)},
    {"SetWindowLongPtrW", reinterpret_cast<PVOID>(Shim_SetWindowLongPtrW)},
    {"SetWindowLongW", reinterpret_cast<PVOID>(Shim_SetWindowLongW)},
    {"GetWindowLongPtrW", reinterpret_cast<PVOID>(Shim_GetWindowLongPtrW)},
    {"GetWindowLongW", reinterpret_cast<PVOID>(Shim_GetWindowLongW)},
    {"GetWindowThreadProcessId", reinterpret_cast<PVOID>(Shim_GetWindowThreadProcessId)},
    {"PeekMessageW", reinterpret_cast<PVOID>(Shim_PeekMessageW)},
    {"GetMessageW", reinterpret_cast<PVOID>(Shim_GetMessageW)},
    {"TranslateMessage", reinterpret_cast<PVOID>(Shim_TranslateMessage)},
    {"DispatchMessageW", reinterpret_cast<PVOID>(Shim_DispatchMessageW)},
    {"PostMessageW", reinterpret_cast<PVOID>(Shim_PostMessageW)},
    {"PostThreadMessageW", reinterpret_cast<PVOID>(Shim_PostThreadMessageW)},
    {"PostQuitMessage", reinterpret_cast<PVOID>(Shim_PostQuitMessage)},
    {"SendMessageW", reinterpret_cast<PVOID>(Shim_SendMessageW)},
    {"GetQueueStatus", reinterpret_cast<PVOID>(Shim_GetQueueStatus)},
    {"InSendMessageEx", reinterpret_cast<PVOID>(Shim_InSendMessageEx)},
    {"IsGUIThread", reinterpret_cast<PVOID>(Shim_IsGUIThread)},
    {"AllowSetForegroundWindow", reinterpret_cast<PVOID>(Shim_AllowSetForegroundWindow)},
    {"SetCoalescableTimer", reinterpret_cast<PVOID>(Shim_SetCoalescableTimer)},
    {"SetTimer", reinterpret_cast<PVOID>(Shim_SetTimer)},
    {"KillTimer", reinterpret_cast<PVOID>(Shim_KillTimer)},
    {"MsgWaitForMultipleObjectsEx", reinterpret_cast<PVOID>(Shim_MsgWaitForMultipleObjectsEx)},
    {"IsThreadMessageQueueAttached", reinterpret_cast<PVOID>(Shim_IsThreadMessageQueueAttached)},
    {"SetThreadInputBlocked", reinterpret_cast<PVOID>(Shim_SetThreadInputBlocked)},
    {"GetProcessWindowStation", reinterpret_cast<PVOID>(Shim_GetProcessWindowStation)},
    {"GetThreadDesktop", reinterpret_cast<PVOID>(Shim_GetThreadDesktop)},
    {"GetUserObjectInformationW", reinterpret_cast<PVOID>(Shim_GetUserObjectInformationW)},
    {"GetClipboardFormatNameW", reinterpret_cast<PVOID>(Shim_GetClipboardFormatNameW)},
    {"MessageBeep", reinterpret_cast<PVOID>(Shim_MessageBeep)},
    {"RegisterClipboardFormatW", reinterpret_cast<PVOID>(Shim_RegisterClipboardFormatW)},
    {"CoInitialize", reinterpret_cast<PVOID>(Shim_CoInitialize)},
    {"CopyEnhMetaFileA", reinterpret_cast<PVOID>(Shim_CopyEnhMetaFileA)},
    {"CreatePenIndirect", reinterpret_cast<PVOID>(Shim_CreatePenIndirect)},
    {"GetTextExtentPoint32A", reinterpret_cast<PVOID>(Shim_GetTextExtentPoint32A)},
    {"SetWinMetaFileBits", reinterpret_cast<PVOID>(Shim_SetWinMetaFileBits)},
    {"CreateIcon", reinterpret_cast<PVOID>(Shim_CreateIcon)},
    {"GetKeyNameTextA", reinterpret_cast<PVOID>(Shim_GetKeyNameTextA)},
    {"ImageList_GetIconSize", reinterpret_cast<PVOID>(Shim_ImageList_GetIconSize)},
    {"ImageList_SetIconSize", reinterpret_cast<PVOID>(Shim_ImageList_SetIconSize)},
};

PVOID ResolveShimFunction(const char* name) {
    if (!name || !*name) {
        return nullptr;
    }
    for (const auto& entry : kExportTable) {
        if (strcmp(entry.Name, name) == 0) {
            return entry.Target;
        }
    }
    return nullptr;
}

void EnsureOriginalFunctionsResolved() {
    if (!g_OrigPresence || !g_OrigPresenceEx) {
        HMODULE hApiQuery = LoadLibraryW(L"api-ms-win-core-apiquery-l1-1-0.dll");
        if (!hApiQuery) {
            hApiQuery = GetModuleHandleW(L"ntdll.dll");
        }
        if (hApiQuery) {
            if (!g_OrigPresence) {
                g_OrigPresence = reinterpret_cast<ApiSetQueryApiSetPresenceFn>(
                    GetProcAddress(hApiQuery, "ApiSetQueryApiSetPresence"));
            }
            if (!g_OrigPresenceEx) {
                g_OrigPresenceEx = reinterpret_cast<ApiSetQueryApiSetPresenceExFn>(
                    GetProcAddress(hApiQuery, "ApiSetQueryApiSetPresenceEx"));
            }
        }
    }
    if (!g_OrigResolveDelayLoad) {
        HMODULE hDelay = LoadLibraryW(L"api-ms-win-core-delayload-l1-1-1.dll");
        if (!hDelay) {
            hDelay = GetModuleHandleW(L"kernelbase.dll");
        }
        if (hDelay) {
            g_OrigResolveDelayLoad = reinterpret_cast<ResolveDelayLoadedApiFn>(
                GetProcAddress(hDelay, "ResolveDelayLoadedAPI"));
        }
    }
    if (!g_OrigGetModuleHandleW) {
        HMODULE hKbase = GetModuleHandleW(L"kernelbase.dll");
        if (hKbase) {
            g_OrigGetModuleHandleW = reinterpret_cast<GetModuleHandleWFn>(
                GetProcAddress(hKbase, "GetModuleHandleW"));
        }
    }
}

void RefreshHooksLocked();

extern "C" int WINAPI Shim_ApiSetQueryApiSetPresence(LPCVOID namespaceName, PBYTE present) {
    EnsureLocksInitialized();
    EnsureOriginalFunctionsResolved();
    if (!IsOsShimDisabledByEnv() && IsShimmedUnicodeString(namespaceName)) {
        EnterCriticalSection(&g_HookLock);
        RefreshHooksLocked();
        LeaveCriticalSection(&g_HookLock);
        if (present) {
            *present = 1;
        }
        return 0;
    }
    return g_OrigPresence ? g_OrigPresence(namespaceName, present) : 0;
}

extern "C" int WINAPI Shim_ApiSetQueryApiSetPresenceEx(LPCVOID namespaceName, PBYTE isInSchema, PBYTE present) {
    EnsureLocksInitialized();
    EnsureOriginalFunctionsResolved();
    if (!IsOsShimDisabledByEnv() && IsShimmedUnicodeString(namespaceName)) {
        EnterCriticalSection(&g_HookLock);
        RefreshHooksLocked();
        LeaveCriticalSection(&g_HookLock);
        if (isInSchema) {
            *isInSchema = 1;
        }
        if (present) {
            *present = 1;
        }
        return 0;
    }
    return g_OrigPresenceEx ? g_OrigPresenceEx(namespaceName, isInSchema, present) : 0;
}

extern "C" PVOID WINAPI Shim_ResolveDelayLoadedAPI(
    PVOID parentModuleBase,
    LPCVOID delayloadDescriptor,
    PVOID failureDllHook,
    PVOID failureSystemHook,
    PVOID* thunkAddress,
    ULONG flags) {
    EnsureLocksInitialized();
    EnsureOriginalFunctionsResolved();

    if (!IsOsShimDisabledByEnv() && parentModuleBase && delayloadDescriptor && thunkAddress) {
        auto* base = static_cast<BYTE*>(parentModuleBase);
        const auto* desc = static_cast<const DelayLoadDescriptor*>(delayloadDescriptor);
        if (desc->DllNameRva != 0) {
            const char* dllName = reinterpret_cast<const char*>(base + desc->DllNameRva);
            if (IsShimmedApiSetAscii(dllName)) {
                auto* iatBase = reinterpret_cast<PVOID*>(base + desc->ImportAddressTableRva);
                auto* intBase = reinterpret_cast<ULONG_PTR*>(base + desc->ImportNameTableRva);
                ptrdiff_t idx = thunkAddress - iatBase;
                if (idx >= 0 && idx < 4096) {
                    ULONG_PTR intEntry = intBase[idx];
                    constexpr ULONG_PTR kOrdinalBit =
                        sizeof(ULONG_PTR) == 8 ? 0x8000000000000000ULL : 0x80000000UL;
                    if (intEntry != 0 && (intEntry & kOrdinalBit) == 0) {
                        const char* funcName = reinterpret_cast<const char*>(
                            base + (intEntry & 0x7FFFFFFFU) + 2);
                        PVOID target = ResolveShimFunction(funcName);
                        if (target) {
                            if (!WritePointerWithProtection(thunkAddress, target)) return nullptr;
                            return target;
                        }
                    }
                }
            }
        }
    }

    return g_OrigResolveDelayLoad
               ? g_OrigResolveDelayLoad(
                     parentModuleBase,
                     delayloadDescriptor,
                     failureDllHook,
                     failureSystemHook,
                     thunkAddress,
                     flags)
               : nullptr;
}

HMODULE WINAPI Shim_GetModuleHandleW(LPCWSTR lpModuleName) {
    if (lpModuleName && IsShimmedApiSetWide(lpModuleName, wcslen(lpModuleName))) {
        SetLastError(kErrorModNotFound);
        return nullptr;
    }
    EnsureOriginalFunctionsResolved();
    return g_OrigGetModuleHandleW ? g_OrigGetModuleHandleW(lpModuleName) : nullptr;
}

bool IsImageRange(const ModuleHookState& state, const void* address, size_t length) {
    ULONG_PTR start = reinterpret_cast<ULONG_PTR>(state.ModuleBase);
    ULONG_PTR target = reinterpret_cast<ULONG_PTR>(address);
    return target >= start && target - start <= state.ImageSize && length <= state.ImageSize - (target - start);
}

bool IsReadableRange(const void* address, size_t length) {
    const BYTE* cursor = static_cast<const BYTE*>(address);
    while (length) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
        size_t available = memory.RegionSize -
            (reinterpret_cast<ULONG_PTR>(cursor) - reinterpret_cast<ULONG_PTR>(memory.BaseAddress));
        size_t span = (std::min)(length, available);
        if (!span) return false;
        cursor += span;
        length -= span;
    }
    return true;
}

bool IsWritableDataRange(const ModuleHookState& state, const void* address,
                         IMAGE_SECTION_HEADER* sections, WORD sectionCount) {
    if (!IsImageRange(state, address, sizeof(int))) return false;
    ULONG_PTR rva = reinterpret_cast<ULONG_PTR>(address) - reinterpret_cast<ULONG_PTR>(state.ModuleBase);
    for (WORD i = 0; i < sectionCount; ++i) {
        const auto& section = sections[i];
        DWORD span = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        if ((section.Characteristics & IMAGE_SCN_MEM_WRITE) != 0 &&
            (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 &&
            rva >= section.VirtualAddress && rva - section.VirtualAddress <= span &&
            sizeof(int) <= span - (rva - section.VirtualAddress)) return true;
    }
    return false;
}

bool IsImageShimmedString(const ModuleHookState& state, const void* address) {
    if (!IsImageRange(state, address, sizeof(UnicodeStringHeader)) ||
        !IsReadableRange(address, sizeof(UnicodeStringHeader))) return false;
    const auto* str = static_cast<const UnicodeStringHeader*>(address);
    if (!str->Buffer || !str->Length || str->Length > 512 || str->Length > str->MaximumLength ||
        (str->Length % sizeof(wchar_t)) != 0 || !IsImageRange(state, str->Buffer, str->Length) ||
        !IsReadableRange(str->Buffer, str->Length)) return false;
    return IsShimmedApiSetWide(str->Buffer, str->Length / sizeof(wchar_t));
}

void DiscoverPresenceCaches(ModuleHookState& state, IMAGE_SECTION_HEADER* sections, WORD sectionCount) {
    auto* base = reinterpret_cast<BYTE*>(state.ModuleBase);
    for (WORD i = 0; i < sectionCount; ++i) {
        if ((sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        DWORD span = (std::min)(sections[i].Misc.VirtualSize, sections[i].SizeOfRawData);
        DWORD start = sections[i].VirtualAddress;
        if (span < 64 || start > state.ImageSize || span > state.ImageSize - start ||
            !IsReadableRange(base + start, span)) continue;
        for (DWORD offset = 48; offset + 16 <= span; ++offset) {
            BYTE* address = base + start + offset;
            LPCVOID stringAddress = nullptr;
            int* cache = nullptr;
#if defined(_M_IX86)
            if (*reinterpret_cast<DWORD*>(address) != 0x41C1940FU ||
                *reinterpret_cast<WORD*>(address + 4) != 0x0D89U ||
                *(address - 44) != 0xA1U || *(address - 21) != 0x68U) continue;
            DWORD first = *reinterpret_cast<DWORD*>(address - 43);
            DWORD second = *reinterpret_cast<DWORD*>(address + 6);
            if (first != second) continue;
            stringAddress = reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(*reinterpret_cast<DWORD*>(address - 20)));
            cache = reinterpret_cast<int*>(static_cast<ULONG_PTR>(second));
#elif defined(_M_X64)
            if (*reinterpret_cast<DWORD*>(address) != 0xD21BD9F6U ||
                *reinterpret_cast<DWORD*>(address + 4) != 0x8902C283U ||
                *(address + 8) != 0x15U || *(address - 48) != 0x8BU || *(address - 47) != 0x0DU ||
                *(address - 22) != 0x48U || *(address - 21) != 0x8DU || *(address - 20) != 0x0DU) continue;
            LONG_PTR firstDisp = *reinterpret_cast<int*>(address - 46);
            LONG_PTR secondDisp = *reinterpret_cast<int*>(address + 9);
            ULONG_PTR first = reinterpret_cast<ULONG_PTR>(address - 42) + firstDisp;
            ULONG_PTR second = reinterpret_cast<ULONG_PTR>(address + 13) + secondDisp;
            if (first != second) continue;
            LONG_PTR stringDisp = *reinterpret_cast<int*>(address - 19);
            stringAddress = reinterpret_cast<LPCVOID>(reinterpret_cast<ULONG_PTR>(address - 15) + stringDisp);
            cache = reinterpret_cast<int*>(second);
#endif
            if (!IsWritableDataRange(state, cache, sections, sectionCount) ||
                !IsReadableRange(cache, sizeof(*cache)) || !IsImageShimmedString(state, stringAddress)) continue;
            if (std::find(state.KnownPresenceCacheAddresses.begin(), state.KnownPresenceCacheAddresses.end(), cache) ==
                state.KnownPresenceCacheAddresses.end()) state.KnownPresenceCacheAddresses.push_back(cache);
        }
    }
}

void TryDiscoverModuleLocked(HMODULE hMod) {
    if (!hMod) {
        return;
    }
    for (const auto& existing : g_HookedModules) {
        if (existing.ModuleBase == hMod) {
            return;
        }
    }

    auto* base = reinterpret_cast<BYTE*>(hMod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }

    ModuleHookState state;
    state.ModuleBase = hMod;
    state.ImageSize = nt->OptionalHeader.SizeOfImage;

    const auto& impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impDir.VirtualAddress != 0) {
        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + impDir.VirtualAddress);
        for (; desc->Name != 0 || desc->FirstThunk != 0; ++desc) {
            DWORD intRva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
            auto* intPtr = reinterpret_cast<ULONG_PTR*>(base + intRva);
            auto* iatPtr = reinterpret_cast<PVOID*>(base + desc->FirstThunk);
            constexpr ULONG_PTR kOrdinalBit =
                sizeof(ULONG_PTR) == 8 ? 0x8000000000000000ULL : 0x80000000UL;
            for (; *intPtr != 0; ++intPtr, ++iatPtr) {
                if ((*intPtr & kOrdinalBit) != 0) {
                    continue;
                }
                const char* funcName = reinterpret_cast<const char*>(base + (*intPtr & 0x7FFFFFFFU) + 2);
                if (strcmp(funcName, "ApiSetQueryApiSetPresence") == 0) {
                    state.PresenceIatSlot = iatPtr;
                    state.OriginalPresenceThunk = g_OrigPresence
                        ? reinterpret_cast<PVOID>(g_OrigPresence)
                        : *iatPtr;
                } else if (strcmp(funcName, "ApiSetQueryApiSetPresenceEx") == 0) {
                    state.PresenceExIatSlot = iatPtr;
                    state.OriginalPresenceExThunk = g_OrigPresenceEx
                        ? reinterpret_cast<PVOID>(g_OrigPresenceEx)
                        : *iatPtr;
                } else if (strcmp(funcName, "ResolveDelayLoadedAPI") == 0) {
                    state.ResolveDelayLoadIatSlot = iatPtr;
                    state.OriginalResolveDelayLoadThunk = g_OrigResolveDelayLoad
                        ? reinterpret_cast<PVOID>(g_OrigResolveDelayLoad)
                        : *iatPtr;
                } else if (strcmp(funcName, "GetModuleHandleW") == 0) {
                    state.GetModuleHandleWIatSlot = iatPtr;
                    state.OriginalGetModuleHandleWThunk = g_OrigGetModuleHandleW
                        ? reinterpret_cast<PVOID>(g_OrigGetModuleHandleW)
                        : *iatPtr;
                }
            }
        }
    }

    const auto& delayDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (delayDir.VirtualAddress != 0) {
        auto* desc = reinterpret_cast<DelayLoadDescriptor*>(base + delayDir.VirtualAddress);
        for (; desc->DllNameRva != 0; ++desc) {
            const char* dllName = reinterpret_cast<const char*>(base + desc->DllNameRva);
            if (!IsShimmedApiSetAscii(dllName)) {
                continue;
            }
            if (desc->ModuleHandleRva != 0) {
                state.ShimmedHmodAddresses.push_back(reinterpret_cast<PVOID*>(base + desc->ModuleHandleRva));
            }
            auto* intPtr = reinterpret_cast<ULONG_PTR*>(base + desc->ImportNameTableRva);
            auto* iatPtr = reinterpret_cast<PVOID*>(base + desc->ImportAddressTableRva);
            constexpr ULONG_PTR kOrdinalBit =
                sizeof(ULONG_PTR) == 8 ? 0x8000000000000000ULL : 0x80000000UL;
            for (; *intPtr != 0; ++intPtr, ++iatPtr) {
                if ((*intPtr & kOrdinalBit) != 0) {
                    continue;
                }
                const char* funcName = reinterpret_cast<const char*>(base + (*intPtr & 0x7FFFFFFFU) + 2);
                DelayLoadSlot slot;
                slot.IatSlot = iatPtr;
                slot.InitialThunk = *iatPtr;
                slot.ShimTarget = ResolveShimFunction(funcName);
                state.ShimmedSlots.push_back(slot);
            }
        }
        auto* sections = IMAGE_FIRST_SECTION(nt);
        DiscoverPresenceCaches(state, sections, nt->FileHeader.NumberOfSections);
    }

    g_HookedModules.push_back(state);
}

bool ApplyModuleState(ModuleHookState& state, bool enableShim) {
    if (state.HasAppliedState && state.LastAppliedEnable == enableShim) return true;
    bool success = true;
    if (state.PresenceIatSlot) {
        success = WritePointerWithProtection(state.PresenceIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_ApiSetQueryApiSetPresence) : state.OriginalPresenceThunk) && success;
    }
    if (state.PresenceExIatSlot) {
        success = WritePointerWithProtection(state.PresenceExIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_ApiSetQueryApiSetPresenceEx) : state.OriginalPresenceExThunk) && success;
    }
    if (state.ResolveDelayLoadIatSlot) {
        success = WritePointerWithProtection(state.ResolveDelayLoadIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_ResolveDelayLoadedAPI) : state.OriginalResolveDelayLoadThunk) && success;
    }
    if (state.GetModuleHandleWIatSlot) {
        success = WritePointerWithProtection(state.GetModuleHandleWIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_GetModuleHandleW) : state.OriginalGetModuleHandleWThunk) && success;
    }
    for (const auto& slot : state.ShimmedSlots) {
        if (!slot.ShimTarget) continue; // Leave unrelated, already-resolved imports alone.
        success = WritePointerWithProtection(slot.IatSlot, enableShim ? slot.ShimTarget : slot.InitialThunk) && success;
    }
    for (PVOID* slot : state.ShimmedHmodAddresses) success = WritePointerWithProtection(slot, nullptr) && success;
    for (int* cache : state.KnownPresenceCacheAddresses) {
        success = WriteInt32WithProtection(cache, enableShim ? kCachedPresent : kCachedNotPresent) && success;
    }
    state.HasAppliedState = success;
    state.LastAppliedEnable = enableShim;
    if (!success) OutputDebugStringW(L"nanoserver_sta_shim: failed to apply or restore hook protection.\n");
    return success;
}

void RefreshHooksLocked() {
    EnsureOriginalFunctionsResolved();
    TryDiscoverModuleLocked(GetModuleHandleW(L"combase.dll"));
    TryDiscoverModuleLocked(GetModuleHandleW(L"user32.dll"));
    TryDiscoverModuleLocked(GetModuleHandleW(L"ole32.dll"));

    bool enable = !IsOsShimDisabledByEnv();
    g_IsActive = enable;
    for (auto& mod : g_HookedModules) {
        if (!ApplyModuleState(mod, enable)) g_IsActive = false;
    }
}

struct LdrDllLoadedNotificationData {
    ULONG Flags;
    const UnicodeStringHeader* FullDllName;
    const UnicodeStringHeader* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
};

union LdrDllNotificationData {
    LdrDllLoadedNotificationData Loaded;
};

using LdrDllNotificationFn = VOID(CALLBACK*)(
    ULONG notificationReason,
    const LdrDllNotificationData* notificationData,
    PVOID context);

using LdrRegisterDllNotificationFn = LONG(WINAPI*)(
    ULONG flags,
    LdrDllNotificationFn notificationFunction,
    PVOID context,
    PVOID* cookie);

PVOID g_LdrNotificationCookie = nullptr;

VOID CALLBACK OnDllNotification(
    ULONG notificationReason,
    const LdrDllNotificationData* notificationData,
    PVOID context) {
    (void)context;
    if (notificationReason == 2 && notificationData) {
        ScopedLock lock(g_HookLock);
        HMODULE module = static_cast<HMODULE>(notificationData->Loaded.DllBase);
        g_HookedModules.erase(std::remove_if(g_HookedModules.begin(), g_HookedModules.end(),
            [module](const ModuleHookState& state) { return state.ModuleBase == module; }), g_HookedModules.end());
        return;
    }
    if (notificationReason != 1 || !notificationData || !notificationData->Loaded.BaseDllName) {
        return;
    }
    const UnicodeStringHeader* name = notificationData->Loaded.BaseDllName;
    if (!name->Buffer || name->Length == 0) {
        return;
    }
    size_t cch = name->Length / sizeof(wchar_t);
    if ((cch == 11 && _wcsnicmp(name->Buffer, L"combase.dll", 11) == 0) ||
        (cch == 10 && _wcsnicmp(name->Buffer, L"user32.dll", 10) == 0) ||
        (cch == 9 && _wcsnicmp(name->Buffer, L"ole32.dll", 9) == 0)) {
        EnsureLocksInitialized();
        EnterCriticalSection(&g_HookLock);
        RefreshHooksLocked();
        LeaveCriticalSection(&g_HookLock);
    }
}

void RegisterLoaderNotification() {
    if (g_LdrNotificationCookie) {
        return;
    }
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) {
        return;
    }
    auto registerFn = reinterpret_cast<LdrRegisterDllNotificationFn>(
        GetProcAddress(hNtdll, "LdrRegisterDllNotification"));
    if (registerFn) {
        registerFn(0, OnDllNotification, nullptr, &g_LdrNotificationCookie);
    }
}

} // namespace

extern "C" void WINAPI NanoServerStaShim_Refresh() {
    EnsureLocksInitialized();
    EnterCriticalSection(&g_HookLock);
    for (auto& mod : g_HookedModules) {
        mod.HasAppliedState = false;
    }
    RefreshHooksLocked();
    LeaveCriticalSection(&g_HookLock);
}

extern "C" void WINAPI NanoServerStaShim_RestoreOriginal() {
    EnsureLocksInitialized();
    EnterCriticalSection(&g_HookLock);
    EnsureOriginalFunctionsResolved();
    TryDiscoverModuleLocked(GetModuleHandleW(L"combase.dll"));
    TryDiscoverModuleLocked(GetModuleHandleW(L"user32.dll"));
    TryDiscoverModuleLocked(GetModuleHandleW(L"ole32.dll"));
    g_IsActive = false;
    for (auto& mod : g_HookedModules) {
        ApplyModuleState(mod, false);
    }
    LeaveCriticalSection(&g_HookLock);
}

extern "C" int WINAPI NanoServerStaShim_IsActive() {
    EnsureLocksInitialized();
    EnterCriticalSection(&g_HookLock);
    int active = (g_IsActive && !IsOsShimDisabledByEnv()) ? 1 : 0;
    LeaveCriticalSection(&g_HookLock);
    return active;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        EnsureLocksInitialized();
        RegisterLoaderNotification();
        EnterCriticalSection(&g_HookLock);
        RefreshHooksLocked();
        LeaveCriticalSection(&g_HookLock);
    }
    return TRUE;
}
