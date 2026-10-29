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
};

struct QueuedMessage {
    HWND HWnd = nullptr;
    UINT Message = 0;
    WPARAM WParam = 0;
    LPARAM LParam = 0;
    DWORD Time = 0;
};

struct SentMessageItem {
    HWND HWnd = nullptr;
    UINT Message = 0;
    WPARAM WParam = 0;
    LPARAM LParam = 0;
    LRESULT Result = 0;
    HANDLE DoneEvent = nullptr;
};

struct ThreadMessageQueue {
    DWORD ThreadId = 0;
    HANDLE WakeEvent = nullptr;
    LONG InSendMessageDepth = 0;
    LONG WaitingCount = 0;
    std::vector<QueuedMessage> Posted;
    std::vector<SentMessageItem*> Sent;
};

struct ClipboardFormatEntry {
    UINT Id = 0;
    std::wstring Name;
};

CRITICAL_SECTION g_HookLock;
CRITICAL_SECTION g_WindowLock;
bool g_LocksInitialized = false;
bool g_IsActive = false;

std::vector<ModuleHookState> g_HookedModules;
std::vector<WindowClassEntry> g_WindowClasses;
std::vector<WindowEntry> g_Windows;
std::vector<ThreadMessageQueue*> g_ThreadQueues;
std::vector<ClipboardFormatEntry> g_ClipboardFormats;

LONG g_NextAtom = 0;
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

void EnsureLocksInitialized() {
    if (!g_LocksInitialized) {
        InitializeCriticalSection(&g_HookLock);
        InitializeCriticalSection(&g_WindowLock);
        g_LocksInitialized = true;
    }
}

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

void WritePointerWithProtection(PVOID* address, PVOID value) {
    DWORD oldProtect = 0;
    if (VirtualProtect(address, sizeof(PVOID), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *address = value;
        VirtualProtect(address, sizeof(PVOID), oldProtect, &oldProtect);
    }
}

void WriteInt32WithProtection(int* address, int value) {
    DWORD oldProtect = 0;
    if (VirtualProtect(address, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *address = value;
        VirtualProtect(address, sizeof(int), oldProtect, &oldProtect);
    }
}

ThreadMessageQueue* GetOrCreateQueueLocked(DWORD threadId) {
    for (ThreadMessageQueue* q : g_ThreadQueues) {
        if (q->ThreadId == threadId) {
            return q;
        }
    }
    auto* q = new ThreadMessageQueue();
    q->ThreadId = threadId;
    q->WakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_ThreadQueues.push_back(q);
    return q;
}

ThreadMessageQueue* GetOrCreateQueue(DWORD threadId) {
    EnterCriticalSection(&g_WindowLock);
    ThreadMessageQueue* q = GetOrCreateQueueLocked(threadId);
    LeaveCriticalSection(&g_WindowLock);
    return q;
}

std::wstring ExtractClassName(LPCWSTR classNamePtr) {
    if (!classNamePtr) {
        return L"";
    }
    ULONG_PTR raw = reinterpret_cast<ULONG_PTR>(classNamePtr);
    if ((raw & ~static_cast<ULONG_PTR>(0xFFFF)) == 0) {
        ATOM atom = static_cast<ATOM>(raw & 0xFFFF);
        EnterCriticalSection(&g_WindowLock);
        for (const auto& cls : g_WindowClasses) {
            if (cls.Atom == atom) {
                std::wstring result = cls.ClassName;
                LeaveCriticalSection(&g_WindowLock);
                return result;
            }
        }
        LeaveCriticalSection(&g_WindowLock);
        wchar_t atomBuf[32] = {};
        swprintf_s(atomBuf, L"#%u", static_cast<unsigned int>(atom));
        return atomBuf;
    }
    return std::wstring(classNamePtr);
}

WindowEntry* FindWindowLocked(HWND hWnd) {
    for (auto& w : g_Windows) {
        if (w.HWnd == hWnd) {
            return &w;
        }
    }
    return nullptr;
}

WNDPROC GetWndProcForWindow(HWND hWnd) {
    if (!hWnd) {
        return nullptr;
    }
    EnterCriticalSection(&g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    WNDPROC proc = entry ? entry->WndProc : nullptr;
    LeaveCriticalSection(&g_WindowLock);
    return proc;
}

LRESULT InvokeWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC proc = GetWndProcForWindow(hWnd);
    if (!proc) {
        return 0;
    }
    return proc(hWnd, msg, wParam, lParam);
}

void ProcessSentMessages(ThreadMessageQueue* queue) {
    while (true) {
        SentMessageItem* item = nullptr;
        EnterCriticalSection(&g_WindowLock);
        if (queue->Sent.empty()) {
            if (queue->Posted.empty()) {
                ResetEvent(queue->WakeEvent);
            }
            LeaveCriticalSection(&g_WindowLock);
            return;
        }
        item = queue->Sent.front();
        queue->Sent.erase(queue->Sent.begin());
        LeaveCriticalSection(&g_WindowLock);

        InterlockedIncrement(&queue->InSendMessageDepth);
        item->Result = InvokeWindowProc(item->HWnd, item->Message, item->WParam, item->LParam);
        InterlockedDecrement(&queue->InSendMessageDepth);
        SetEvent(item->DoneEvent);
    }
}

bool MatchesWindowFilter(HWND msgHwnd, HWND filterHwnd) {
    if (!filterHwnd) {
        return true;
    }
    if (filterHwnd == reinterpret_cast<HWND>(static_cast<LONG_PTR>(-1))) {
        return msgHwnd == nullptr;
    }
    return msgHwnd == filterHwnd;
}

bool MatchesMessageFilter(UINT msg, UINT minFilter, UINT maxFilter) {
    return (minFilter == 0 && maxFilter == 0) || (msg >= minFilter && msg <= maxFilter);
}

bool TryDequeueMessage(
    ThreadMessageQueue* queue,
    HWND hWndFilter,
    UINT minFilter,
    UINT maxFilter,
    bool remove,
    QueuedMessage* outMsg) {
    EnterCriticalSection(&g_WindowLock);
    for (size_t i = 0; i < queue->Posted.size(); ++i) {
        const QueuedMessage& candidate = queue->Posted[i];
        if (!MatchesWindowFilter(candidate.HWnd, hWndFilter) ||
            !MatchesMessageFilter(candidate.Message, minFilter, maxFilter)) {
            continue;
        }
        *outMsg = candidate;
        if (remove) {
            queue->Posted.erase(queue->Posted.begin() + static_cast<ptrdiff_t>(i));
            if (queue->Posted.empty() && queue->Sent.empty()) {
                ResetEvent(queue->WakeEvent);
            }
        }
        LeaveCriticalSection(&g_WindowLock);
        return true;
    }
    LeaveCriticalSection(&g_WindowLock);
    return false;
}

bool QueueHasMessages(ThreadMessageQueue* queue) {
    EnterCriticalSection(&g_WindowLock);
    bool has = !queue->Posted.empty() || !queue->Sent.empty();
    LeaveCriticalSection(&g_WindowLock);
    return has;
}

ATOM RegisterClassCommon(
    const std::wstring& className,
    UINT style,
    WNDPROC wndProc,
    int clsExtra,
    int wndExtra,
    HINSTANCE hInstance) {
    if (className.empty()) {
        return 0;
    }
    EnterCriticalSection(&g_WindowLock);
    for (auto& cls : g_WindowClasses) {
        if (EqualsIgnoreCaseWide(cls.ClassName, className)) {
            cls.Style = style;
            cls.WndProc = wndProc;
            cls.ClsExtra = clsExtra;
            cls.WndExtra = wndExtra;
            cls.HInstance = hInstance;
            ATOM atom = cls.Atom;
            LeaveCriticalSection(&g_WindowLock);
            return atom;
        }
    }
    LONG next = InterlockedIncrement(&g_NextAtom);
    ATOM atom = static_cast<ATOM>(0xC000 | (next & 0x3FFF));
    WindowClassEntry entry;
    entry.Atom = atom;
    entry.ClassName = className;
    entry.Style = style;
    entry.WndProc = wndProc;
    entry.ClsExtra = clsExtra;
    entry.WndExtra = wndExtra;
    entry.HInstance = hInstance;
    g_WindowClasses.push_back(entry);
    LeaveCriticalSection(&g_WindowLock);
    return atom;
}

ATOM WINAPI Shim_RegisterClassW(const WNDCLASSW* lpWndClass) {
    if (!lpWndClass) {
        return 0;
    }
    std::wstring className = ExtractClassName(lpWndClass->lpszClassName);
    return RegisterClassCommon(
        className,
        lpWndClass->style,
        lpWndClass->lpfnWndProc,
        lpWndClass->cbClsExtra,
        lpWndClass->cbWndExtra,
        lpWndClass->hInstance);
}

ATOM WINAPI Shim_RegisterClassExW(const WNDCLASSEXW* lpwcx) {
    if (!lpwcx) {
        return 0;
    }
    std::wstring className = ExtractClassName(lpwcx->lpszClassName);
    return RegisterClassCommon(
        className,
        lpwcx->style,
        lpwcx->lpfnWndProc,
        lpwcx->cbClsExtra,
        lpwcx->cbWndExtra,
        lpwcx->hInstance);
}

BOOL WINAPI Shim_UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance) {
    (void)hInstance;
    if (!lpClassName) {
        return FALSE;
    }
    std::wstring className = ExtractClassName(lpClassName);
    EnterCriticalSection(&g_WindowLock);
    for (auto it = g_WindowClasses.begin(); it != g_WindowClasses.end(); ++it) {
        if (EqualsIgnoreCaseWide(it->ClassName, className)) {
            g_WindowClasses.erase(it);
            break;
        }
    }
    LeaveCriticalSection(&g_WindowLock);
    return TRUE;
}

LRESULT WINAPI Shim_DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    (void)hWnd;
    (void)Msg;
    (void)wParam;
    (void)lParam;
    return 0;
}

HWND WINAPI Shim_CreateWindowExW(
    DWORD dwExStyle,
    LPCWSTR lpClassName,
    LPCWSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam) {
    (void)X;
    (void)Y;
    (void)nWidth;
    (void)nHeight;
    (void)lpParam;

    std::wstring className = ExtractClassName(lpClassName);
    WNDPROC wndProc = Shim_DefWindowProcW;
    int wndExtra = 0;

    EnterCriticalSection(&g_WindowLock);
    for (const auto& cls : g_WindowClasses) {
        if (EqualsIgnoreCaseWide(cls.ClassName, className)) {
            if (cls.WndProc) {
                wndProc = cls.WndProc;
            }
            if (cls.WndExtra > 0) {
                wndExtra = cls.WndExtra;
            }
            break;
        }
    }

    DWORD threadId = GetCurrentThreadId();
    GetOrCreateQueueLocked(threadId);
    HWND hWnd = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(InterlockedIncrement(&g_NextHwnd)));

    WindowEntry entry;
    entry.HWnd = hWnd;
    entry.ThreadId = threadId;
    entry.WndProc = wndProc;
    entry.Style = dwStyle;
    entry.ExStyle = dwExStyle;
    entry.Parent = hWndParent;
    entry.Menu = hMenu;
    entry.HInstance = hInstance;
    entry.UserData = 0;
    entry.ClassName = className;
    entry.WindowName = lpWindowName ? std::wstring(lpWindowName) : L"";
    int allocExtra = wndExtra > 64 ? wndExtra : 64;
    entry.ExtraBytes.resize(static_cast<size_t>(allocExtra), 0);
    g_Windows.push_back(entry);
    LeaveCriticalSection(&g_WindowLock);

    return hWnd;
}

BOOL WINAPI Shim_DestroyWindow(HWND hWnd) {
    EnterCriticalSection(&g_WindowLock);
    for (auto it = g_Windows.begin(); it != g_Windows.end(); ++it) {
        if (it->HWnd == hWnd) {
            g_Windows.erase(it);
            LeaveCriticalSection(&g_WindowLock);
            return TRUE;
        }
    }
    LeaveCriticalSection(&g_WindowLock);
    SetLastError(kErrorInvalidWindowHandle);
    return FALSE;
}

BOOL WINAPI Shim_IsWindow(HWND hWnd) {
    if (!hWnd) {
        return FALSE;
    }
    EnterCriticalSection(&g_WindowLock);
    bool exists = FindWindowLocked(hWnd) != nullptr;
    LeaveCriticalSection(&g_WindowLock);
    return exists ? TRUE : FALSE;
}

BOOL WINAPI Shim_IsChild(HWND hWndParent, HWND hWnd) {
    if (!hWndParent || !hWnd) {
        return FALSE;
    }
    EnterCriticalSection(&g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    bool isChild = entry && entry->Parent == hWndParent;
    LeaveCriticalSection(&g_WindowLock);
    return isChild ? TRUE : FALSE;
}

LONG_PTR WINAPI Shim_GetWindowLongPtrW(HWND hWnd, int nIndex) {
    EnterCriticalSection(&g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    if (!entry) {
        LeaveCriticalSection(&g_WindowLock);
        SetLastError(kErrorInvalidWindowHandle);
        return 0;
    }
    LONG_PTR ret = 0;
    switch (nIndex) {
        case kGwlpWndProc:
            ret = reinterpret_cast<LONG_PTR>(entry->WndProc);
            break;
        case kGwlpHInstance:
            ret = reinterpret_cast<LONG_PTR>(entry->HInstance);
            break;
        case kGwlpHwndParent:
            ret = reinterpret_cast<LONG_PTR>(entry->Parent);
            break;
        case kGwlpId:
            ret = reinterpret_cast<LONG_PTR>(entry->Menu);
            break;
        case kGwlStyle:
            ret = static_cast<LONG_PTR>(entry->Style);
            break;
        case kGwlExStyle:
            ret = static_cast<LONG_PTR>(entry->ExStyle);
            break;
        case kGwlpUserData:
            ret = entry->UserData;
            break;
        default:
            if (nIndex >= 0 && static_cast<size_t>(nIndex) + sizeof(LONG_PTR) <= entry->ExtraBytes.size()) {
                ret = *reinterpret_cast<LONG_PTR*>(&entry->ExtraBytes[static_cast<size_t>(nIndex)]);
            }
            break;
    }
    LeaveCriticalSection(&g_WindowLock);
    return ret;
}

LONG_PTR WINAPI Shim_SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    EnterCriticalSection(&g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    if (!entry) {
        LeaveCriticalSection(&g_WindowLock);
        SetLastError(kErrorInvalidWindowHandle);
        return 0;
    }
    LONG_PTR prev = 0;
    switch (nIndex) {
        case kGwlpWndProc:
            prev = reinterpret_cast<LONG_PTR>(entry->WndProc);
            entry->WndProc = reinterpret_cast<WNDPROC>(dwNewLong);
            break;
        case kGwlpHInstance:
            prev = reinterpret_cast<LONG_PTR>(entry->HInstance);
            entry->HInstance = reinterpret_cast<HINSTANCE>(dwNewLong);
            break;
        case kGwlpHwndParent:
            prev = reinterpret_cast<LONG_PTR>(entry->Parent);
            entry->Parent = reinterpret_cast<HWND>(dwNewLong);
            break;
        case kGwlpId:
            prev = reinterpret_cast<LONG_PTR>(entry->Menu);
            entry->Menu = reinterpret_cast<HMENU>(dwNewLong);
            break;
        case kGwlStyle:
            prev = static_cast<LONG_PTR>(entry->Style);
            entry->Style = static_cast<DWORD>(dwNewLong);
            break;
        case kGwlExStyle:
            prev = static_cast<LONG_PTR>(entry->ExStyle);
            entry->ExStyle = static_cast<DWORD>(dwNewLong);
            break;
        case kGwlpUserData:
            prev = entry->UserData;
            entry->UserData = dwNewLong;
            break;
        default:
            if (nIndex >= 0) {
                size_t needed = static_cast<size_t>(nIndex) + sizeof(LONG_PTR);
                if (entry->ExtraBytes.size() < needed) {
                    entry->ExtraBytes.resize(needed, 0);
                }
                prev = *reinterpret_cast<LONG_PTR*>(&entry->ExtraBytes[static_cast<size_t>(nIndex)]);
                *reinterpret_cast<LONG_PTR*>(&entry->ExtraBytes[static_cast<size_t>(nIndex)]) = dwNewLong;
            }
            break;
    }
    LeaveCriticalSection(&g_WindowLock);
    return prev;
}

LONG WINAPI Shim_GetWindowLongW(HWND hWnd, int nIndex) {
    return static_cast<LONG>(Shim_GetWindowLongPtrW(hWnd, nIndex));
}

LONG WINAPI Shim_SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) {
    return static_cast<LONG>(Shim_SetWindowLongPtrW(hWnd, nIndex, static_cast<LONG_PTR>(dwNewLong)));
}

DWORD WINAPI Shim_GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) {
    if (lpdwProcessId) {
        *lpdwProcessId = GetCurrentProcessId();
    }
    if (!hWnd) {
        return 0;
    }
    EnterCriticalSection(&g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    DWORD tid = entry ? entry->ThreadId : 0;
    LeaveCriticalSection(&g_WindowLock);
    if (!tid) {
        SetLastError(kErrorInvalidWindowHandle);
    }
    return tid;
}

void WriteMsgStruct(LPMSG lpMsg, const QueuedMessage& msg) {
    if (!lpMsg) {
        return;
    }
    lpMsg->hwnd = msg.HWnd;
    lpMsg->message = msg.Message;
    lpMsg->wParam = msg.WParam;
    lpMsg->lParam = msg.LParam;
    lpMsg->time = msg.Time;
    lpMsg->pt.x = 0;
    lpMsg->pt.y = 0;
}

BOOL WINAPI Shim_PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    ThreadMessageQueue* queue = GetOrCreateQueue(GetCurrentThreadId());
    ProcessSentMessages(queue);
    bool remove = (wRemoveMsg & 0x0001u) != 0;
    QueuedMessage msg;
    if (!TryDequeueMessage(queue, hWnd, wMsgFilterMin, wMsgFilterMax, remove, &msg)) {
        return FALSE;
    }
    WriteMsgStruct(lpMsg, msg);
    return TRUE;
}

BOOL WINAPI Shim_GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    ThreadMessageQueue* queue = GetOrCreateQueue(GetCurrentThreadId());
    while (true) {
        ProcessSentMessages(queue);
        QueuedMessage msg;
        if (TryDequeueMessage(queue, hWnd, wMsgFilterMin, wMsgFilterMax, true, &msg)) {
            WriteMsgStruct(lpMsg, msg);
            return msg.Message == kWmQuit ? FALSE : TRUE;
        }
        InterlockedIncrement(&queue->WaitingCount);
        WaitForSingleObject(queue->WakeEvent, 50);
        InterlockedDecrement(&queue->WaitingCount);
    }
}

BOOL WINAPI Shim_TranslateMessage(const MSG* lpMsg) {
    (void)lpMsg;
    return FALSE;
}

LRESULT WINAPI Shim_DispatchMessageW(const MSG* lpMsg) {
    if (!lpMsg) {
        return 0;
    }
    if (lpMsg->message == kWmTimer && lpMsg->lParam != 0) {
        auto timerProc = reinterpret_cast<TIMERPROC>(lpMsg->lParam);
        timerProc(lpMsg->hwnd, kWmTimer, static_cast<UINT_PTR>(lpMsg->wParam), GetTickCount());
        return 0;
    }
    if (!lpMsg->hwnd) {
        return 0;
    }
    return InvokeWindowProc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
}

BOOL WINAPI Shim_PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    DWORD targetThreadId = 0;
    EnterCriticalSection(&g_WindowLock);
    if (!hWnd) {
        targetThreadId = GetCurrentThreadId();
    } else {
        WindowEntry* entry = FindWindowLocked(hWnd);
        if (!entry) {
            LeaveCriticalSection(&g_WindowLock);
            SetLastError(kErrorInvalidWindowHandle);
            return FALSE;
        }
        targetThreadId = entry->ThreadId;
    }
    ThreadMessageQueue* queue = GetOrCreateQueueLocked(targetThreadId);
    QueuedMessage qm{hWnd, Msg, wParam, lParam, GetTickCount()};
    queue->Posted.push_back(qm);
    SetEvent(queue->WakeEvent);
    LeaveCriticalSection(&g_WindowLock);
    return TRUE;
}

BOOL WINAPI Shim_PostThreadMessageW(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam) {
    EnterCriticalSection(&g_WindowLock);
    ThreadMessageQueue* queue = GetOrCreateQueueLocked(idThread);
    QueuedMessage qm{nullptr, Msg, wParam, lParam, GetTickCount()};
    queue->Posted.push_back(qm);
    SetEvent(queue->WakeEvent);
    LeaveCriticalSection(&g_WindowLock);
    return TRUE;
}

void WINAPI Shim_PostQuitMessage(int nExitCode) {
    Shim_PostThreadMessageW(GetCurrentThreadId(), kWmQuit, static_cast<WPARAM>(nExitCode), 0);
}

LRESULT WINAPI Shim_SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    EnterCriticalSection(&g_WindowLock);
    WindowEntry* entry = FindWindowLocked(hWnd);
    if (!entry) {
        LeaveCriticalSection(&g_WindowLock);
        SetLastError(kErrorInvalidWindowHandle);
        return 0;
    }
    DWORD targetThreadId = entry->ThreadId;
    LeaveCriticalSection(&g_WindowLock);

    DWORD currentThreadId = GetCurrentThreadId();
    if (targetThreadId == currentThreadId) {
        return InvokeWindowProc(hWnd, Msg, wParam, lParam);
    }

    ThreadMessageQueue* targetQueue = GetOrCreateQueue(targetThreadId);
    ThreadMessageQueue* currentQueue = GetOrCreateQueue(currentThreadId);

    SentMessageItem item;
    item.HWnd = hWnd;
    item.Message = Msg;
    item.WParam = wParam;
    item.LParam = lParam;
    item.Result = 0;
    item.DoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    EnterCriticalSection(&g_WindowLock);
    targetQueue->Sent.push_back(&item);
    SetEvent(targetQueue->WakeEvent);
    LONG waiting = targetQueue->WaitingCount;
    LeaveCriticalSection(&g_WindowLock);

    DWORD timeoutMs = waiting > 0 ? 500 : 25;
    DWORD elapsed = 0;
    bool completed = false;
    while (elapsed < timeoutMs) {
        ProcessSentMessages(currentQueue);
        if (WaitForSingleObject(item.DoneEvent, 5) == WAIT_OBJECT_0) {
            completed = true;
            break;
        }
        elapsed += 5;
    }

    if (!completed) {
        EnterCriticalSection(&g_WindowLock);
        for (auto it = targetQueue->Sent.begin(); it != targetQueue->Sent.end(); ++it) {
            if (*it == &item) {
                targetQueue->Sent.erase(it);
                break;
            }
        }
        LeaveCriticalSection(&g_WindowLock);
        if (WaitForSingleObject(item.DoneEvent, 0) != WAIT_OBJECT_0) {
            item.Result = InvokeWindowProc(hWnd, Msg, wParam, lParam);
        }
    }

    CloseHandle(item.DoneEvent);
    return item.Result;
}

DWORD WINAPI Shim_GetQueueStatus(UINT flags) {
    (void)flags;
    ThreadMessageQueue* queue = GetOrCreateQueue(GetCurrentThreadId());
    return QueueHasMessages(queue) ? 0x00FF00FFu : 0u;
}

DWORD WINAPI Shim_InSendMessageEx(LPVOID lpReserved) {
    (void)lpReserved;
    ThreadMessageQueue* queue = GetOrCreateQueue(GetCurrentThreadId());
    return InterlockedCompareExchange(&queue->InSendMessageDepth, 0, 0) > 0 ? 1u : 0u;
}

BOOL WINAPI Shim_IsGUIThread(BOOL bConvert) {
    (void)bConvert;
    GetOrCreateQueue(GetCurrentThreadId());
    return TRUE;
}

BOOL WINAPI Shim_AllowSetForegroundWindow(DWORD dwProcessId) {
    (void)dwProcessId;
    return TRUE;
}

UINT_PTR WINAPI Shim_SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) {
    (void)hWnd;
    (void)uElapse;
    (void)lpTimerFunc;
    if (nIDEvent != 0) {
        return nIDEvent;
    }
    return static_cast<UINT_PTR>(InterlockedIncrement(&g_NextTimerId));
}

UINT_PTR WINAPI Shim_SetCoalescableTimer(
    HWND hWnd,
    UINT_PTR nIDEvent,
    UINT uElapse,
    TIMERPROC lpTimerFunc,
    ULONG uToleranceDelay) {
    (void)uToleranceDelay;
    return Shim_SetTimer(hWnd, nIDEvent, uElapse, lpTimerFunc);
}

BOOL WINAPI Shim_KillTimer(HWND hWnd, UINT_PTR uIDEvent) {
    (void)hWnd;
    (void)uIDEvent;
    return TRUE;
}

DWORD WINAPI Shim_MsgWaitForMultipleObjectsEx(
    DWORD nCount,
    const HANDLE* pHandles,
    DWORD dwMilliseconds,
    DWORD dwWakeMask,
    DWORD dwFlags) {
    (void)dwWakeMask;
    ThreadMessageQueue* queue = GetOrCreateQueue(GetCurrentThreadId());
    ProcessSentMessages(queue);
    if (QueueHasMessages(queue)) {
        return nCount;
    }

    HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
    DWORD copyCount = nCount < (MAXIMUM_WAIT_OBJECTS - 1) ? nCount : (MAXIMUM_WAIT_OBJECTS - 1);
    for (DWORD i = 0; i < copyCount; ++i) {
        handles[i] = pHandles[i];
    }
    handles[copyCount] = queue->WakeEvent;

    BOOL waitAll = (dwFlags & 0x0001u) != 0 ? TRUE : FALSE;
    BOOL alertable = (dwFlags & 0x0002u) != 0 ? TRUE : FALSE;

    InterlockedIncrement(&queue->WaitingCount);
    DWORD res = WaitForMultipleObjectsEx(copyCount + 1, handles, waitAll, dwMilliseconds, alertable);
    InterlockedDecrement(&queue->WaitingCount);

    ProcessSentMessages(queue);
    return res;
}

BOOL WINAPI Shim_IsThreadMessageQueueAttached() {
    return TRUE;
}

BOOL WINAPI Shim_SetThreadInputBlocked(BOOL blocked) {
    (void)blocked;
    return TRUE;
}

HANDLE WINAPI Shim_GetProcessWindowStation() {
    return kFakeWinSta;
}

HANDLE WINAPI Shim_GetThreadDesktop(DWORD dwThreadId) {
    (void)dwThreadId;
    return kFakeDesktop;
}

BOOL WINAPI Shim_GetUserObjectInformationW(
    HANDLE hObj,
    int nIndex,
    PVOID pvInfo,
    DWORD nLength,
    LPDWORD lpnLengthNeeded) {
    if (nIndex != kUoiName) {
        if (lpnLengthNeeded) {
            *lpnLengthNeeded = 0;
        }
        return TRUE;
    }
    const wchar_t* name = (hObj == kFakeWinSta) ? L"WinSta0" : L"Default";
    DWORD lenChars = static_cast<DWORD>(wcslen(name));
    DWORD requiredBytes = (lenChars + 1) * sizeof(wchar_t);
    if (lpnLengthNeeded) {
        *lpnLengthNeeded = requiredBytes;
    }
    if (!pvInfo || nLength < requiredBytes) {
        return FALSE;
    }
    memcpy(pvInfo, name, requiredBytes);
    return TRUE;
}

UINT WINAPI Shim_RegisterClipboardFormatW(LPCWSTR lpszFormat) {
    if (!lpszFormat || !*lpszFormat) {
        return 0;
    }
    std::wstring fmt(lpszFormat);
    EnterCriticalSection(&g_WindowLock);
    for (const auto& entry : g_ClipboardFormats) {
        if (EqualsIgnoreCaseWide(entry.Name, fmt)) {
            UINT id = entry.Id;
            LeaveCriticalSection(&g_WindowLock);
            return id;
        }
    }
    UINT id = static_cast<UINT>(InterlockedIncrement(&g_NextClipboardFormat));
    g_ClipboardFormats.push_back(ClipboardFormatEntry{id, fmt});
    LeaveCriticalSection(&g_WindowLock);
    return id;
}

int WINAPI Shim_GetClipboardFormatNameW(UINT format, LPWSTR lpszFormatName, int cchMaxCount) {
    if (!lpszFormatName || cchMaxCount <= 0) {
        return 0;
    }
    std::wstring name;
    EnterCriticalSection(&g_WindowLock);
    for (const auto& entry : g_ClipboardFormats) {
        if (entry.Id == format) {
            name = entry.Name;
            break;
        }
    }
    LeaveCriticalSection(&g_WindowLock);
    if (name.empty()) {
        wchar_t buf[32] = {};
        swprintf_s(buf, L"Format_%X", format);
        name = buf;
    }
    int copyLen = static_cast<int>(name.size());
    if (copyLen >= cchMaxCount) {
        copyLen = cchMaxCount - 1;
    }
    memcpy(lpszFormatName, name.data(), static_cast<size_t>(copyLen) * sizeof(wchar_t));
    lpszFormatName[copyLen] = L'\0';
    return copyLen;
}

BOOL WINAPI Shim_MessageBeep(UINT uType) {
    (void)uType;
    return TRUE;
}

HRESULT WINAPI Shim_CoInitialize(LPVOID pvReserved) {
    return CoInitializeEx(pvReserved, 0x2 /* COINIT_APARTMENTTHREADED */);
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
                            WritePointerWithProtection(thunkAddress, target);
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

void DiscoverPresenceCaches(ModuleHookState& state, IMAGE_SECTION_HEADER* sections, WORD numSections) {
    auto* base = reinterpret_cast<BYTE*>(state.ModuleBase);
    for (WORD i = 0; i < numSections; ++i) {
        if ((sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }
        DWORD span = sections[i].Misc.VirtualSize < sections[i].SizeOfRawData
                         ? sections[i].Misc.VirtualSize
                         : sections[i].SizeOfRawData;
        if (span < 64) {
            continue;
        }
        DWORD startRva = sections[i].VirtualAddress;
#if defined(_M_IX86)
        DWORD endRva = startRva + span - 12;
        for (DWORD rva = startRva + 44; rva <= endRva; ++rva) {
            BYTE* addr = base + rva;
            if (*reinterpret_cast<DWORD*>(addr) != 0x41C1940FU ||
                *reinterpret_cast<WORD*>(addr + 4) != 0x0D89U ||
                *(addr - 44) != 0xA1U ||
                *(addr - 21) != 0x68U) {
                continue;
            }
            DWORD cacheVa1 = *reinterpret_cast<DWORD*>(addr - 43);
            DWORD cacheVa2 = *reinterpret_cast<DWORD*>(addr + 6);
            if (cacheVa1 != cacheVa2) {
                continue;
            }
            auto* ustrAddr = reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(*reinterpret_cast<DWORD*>(addr - 20)));
            if (IsShimmedUnicodeString(ustrAddr)) {
                int* cachePtr = reinterpret_cast<int*>(static_cast<ULONG_PTR>(cacheVa2));
                bool found = false;
                for (int* existing : state.KnownPresenceCacheAddresses) {
                    if (existing == cachePtr) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    state.KnownPresenceCacheAddresses.push_back(cachePtr);
                }
            }
        }
#elif defined(_M_X64)
        DWORD endRva = startRva + span - 16;
        for (DWORD rva = startRva + 48; rva <= endRva; ++rva) {
            BYTE* addr = base + rva;
            if (*reinterpret_cast<DWORD*>(addr) != 0xD21BD9F6U ||
                *reinterpret_cast<DWORD*>(addr + 4) != 0x8902C283U ||
                *(addr + 8) != 0x15U ||
                *(addr - 48) != 0x8BU ||
                *(addr - 47) != 0x0DU ||
                *(addr - 22) != 0x48U ||
                *(addr - 21) != 0x8DU ||
                *(addr - 20) != 0x0DU) {
                continue;
            }
            int disp1 = *reinterpret_cast<int*>(addr - 46);
            int disp2 = *reinterpret_cast<int*>(addr + 9);
            BYTE* cacheAddr1 = addr - 42 + disp1;
            BYTE* cacheAddr2 = addr + 13 + disp2;
            if (cacheAddr1 != cacheAddr2) {
                continue;
            }
            int ustrDisp = *reinterpret_cast<int*>(addr - 19);
            LPCVOID ustrAddr = addr - 15 + ustrDisp;
            if (IsShimmedUnicodeString(ustrAddr)) {
                int* cachePtr = reinterpret_cast<int*>(cacheAddr2);
                bool found = false;
                for (int* existing : state.KnownPresenceCacheAddresses) {
                    if (existing == cachePtr) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    state.KnownPresenceCacheAddresses.push_back(cachePtr);
                }
            }
        }
#endif
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

void ApplyModuleState(ModuleHookState& state, bool enableShim) {
    if (state.PresenceIatSlot) {
        WritePointerWithProtection(
            state.PresenceIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_ApiSetQueryApiSetPresence)
                       : state.OriginalPresenceThunk);
    }
    if (state.PresenceExIatSlot) {
        WritePointerWithProtection(
            state.PresenceExIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_ApiSetQueryApiSetPresenceEx)
                       : state.OriginalPresenceExThunk);
    }
    if (state.ResolveDelayLoadIatSlot) {
        WritePointerWithProtection(
            state.ResolveDelayLoadIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_ResolveDelayLoadedAPI)
                       : state.OriginalResolveDelayLoadThunk);
    }
    if (state.GetModuleHandleWIatSlot) {
        WritePointerWithProtection(
            state.GetModuleHandleWIatSlot,
            enableShim ? reinterpret_cast<PVOID>(Shim_GetModuleHandleW)
                       : state.OriginalGetModuleHandleWThunk);
    }

    for (const auto& slot : state.ShimmedSlots) {
        PVOID target = (enableShim && slot.ShimTarget) ? slot.ShimTarget : slot.InitialThunk;
        WritePointerWithProtection(slot.IatSlot, target);
    }
    for (PVOID* hmodSlot : state.ShimmedHmodAddresses) {
        WritePointerWithProtection(hmodSlot, nullptr);
    }
    int cacheState = enableShim ? kCachedPresent : kCachedNotPresent;
    for (int* cachePtr : state.KnownPresenceCacheAddresses) {
        WriteInt32WithProtection(cachePtr, cacheState);
    }
}

void RefreshHooksLocked() {
    EnsureOriginalFunctionsResolved();
    TryDiscoverModuleLocked(GetModuleHandleW(L"combase.dll"));
    TryDiscoverModuleLocked(GetModuleHandleW(L"user32.dll"));
    TryDiscoverModuleLocked(GetModuleHandleW(L"ole32.dll"));

    bool enable = !IsOsShimDisabledByEnv();
    g_IsActive = enable;
    for (auto& mod : g_HookedModules) {
        ApplyModuleState(mod, enable);
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
