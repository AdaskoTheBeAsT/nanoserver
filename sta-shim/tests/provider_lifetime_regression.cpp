// Deliberately link only kernel32. A user32 import would retain the provider
// and hide callbacks left behind after the last explicit LoadLibrary handle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

namespace {
int failures = 0;
void Check(const char* name, bool passed) {
    std::printf("CHECK %s %s error=%lu\n", name, passed ? "PASS" : "FAIL", GetLastError());
    if (!passed) ++failures;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (GetFileAttributesW(L"C:\\Windows\\System32\\win32u.dll") != INVALID_FILE_ATTRIBUTES)
        return 64;
    Check("provider_lifetime_no_static_user32_reference", GetModuleHandleW(L"user32.dll") == nullptr);
    HMODULE provider = LoadLibraryExW(L"nanoserver_sta_shim.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    Check("provider_lifetime_loaded", provider != nullptr);
    if (!provider) return 1;
    using CapabilitiesFn = DWORD (WINAPI*)(DWORD);
    auto capabilities = reinterpret_cast<CapabilitiesFn>(GetProcAddress(provider, "NanoServerStaShim_GetCapabilities"));
    Check("provider_lifetime_callback_export", capabilities != nullptr);
    if (!capabilities) return 1;
    Check("provider_lifetime_explicit_reference_released", FreeLibrary(provider) != FALSE);
    HMODULE resident = nullptr;
    bool mapped = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(capabilities), &resident) && resident == provider;
    Check("provider_lifetime_callback_owner_stays_mapped", mapped);
    Check("provider_lifetime_code_callable_after_release", mapped && capabilities(0xFFFFFFFF) == 0);
    HMODULE companion = LoadLibraryExW(L"comsvcs.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    Check("provider_lifetime_late_loader_notification", companion != nullptr);
    if (companion) FreeLibrary(companion);
    std::printf("SUMMARY provider_lifetime bits=%zu failures=%d\n", sizeof(void*) * 8, failures);
    // A successful summary is not enough: real process teardown must exit zero.
    return failures ? 1 : 0;
}
