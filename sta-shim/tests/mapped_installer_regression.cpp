// Disposable Nano-container-only test. Keep the exact forwarder images mapped
// while two real installer passes replace their directory entries.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2 || argc > 3 ||
        (argc == 3 && std::wcscmp(argv[2], L"--require-wow64")) ||
        GetFileAttributesW(L"C:\\Windows\\System32\\win32u.dll") != INVALID_FILE_ATTRIBUTES)
        return 64;
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH)) return 1;
    const wchar_t* names[] = {L"kernel32.dll", L"user32.dll", L"ole32.dll", L"gdi32.dll",
        L"comctl32.dll", L"shell32.dll", L"winmm.dll", L"shlwapi.dll"};
    HMODULE modules[8]{};
    for (size_t i = 0; i != 8; ++i) {
        wchar_t path[MAX_PATH]{};
        if (swprintf_s(path, L"%s\\forwarders\\%s", system, names[i]) < 0) return 1;
        modules[i] = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!modules[i]) {
            std::printf("FAIL load_forwarder=%ls error=%lu\n", names[i], GetLastError());
            return 1;
        }
        wchar_t loaded[MAX_PATH]{};
        if (!GetModuleFileNameW(modules[i], loaded, MAX_PATH) || _wcslwr_s(loaded) ||
            !std::wcsstr(loaded, L"\\forwarders\\")) {
            std::printf("FAIL expected_forwarder=%ls actual=%ls\n", names[i], loaded);
            return 1;
        }
        std::printf("OBSERVE held_forwarder=%ls path=%ls\n", names[i], loaded);
    }
    using ActiveFn = int (WINAPI*)();
    HMODULE shim = GetModuleHandleW(L"nanoserver_sta_shim.dll");
    auto active = shim ? reinterpret_cast<ActiveFn>(GetProcAddress(shim, "NanoServerStaShim_IsActive")) : nullptr;
    if (!active || !active()) return 64;

    for (int pass = 1; pass <= 2; ++pass) {
        wchar_t command[MAX_PATH * 3]{};
        if (swprintf_s(command, L"\"%s\\install_sta_shim.exe\" \"%s\" %s",
                argv[1], argv[1], argc == 3 ? argv[2] : L"") < 0) return 1;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command, nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                &startup, &process)) {
            std::printf("FAIL installer_create error=%lu\n", GetLastError());
            return 1;
        }
        DWORD wait = WaitForSingleObject(process.hProcess, 120000);
        DWORD code = 0xFFFFFFFF;
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(process.hProcess, 30000);
        }
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::printf("OBSERVE mapped_installer pass=%d wait=%lu exit=0x%08lX\n", pass, wait, code);
        if (wait != WAIT_OBJECT_0 || code) return 1;
        for (size_t i = 0; i != 8; ++i) {
            MEMORY_BASIC_INFORMATION region{};
            if (reinterpret_cast<const IMAGE_DOS_HEADER*>(modules[i])->e_magic != IMAGE_DOS_SIGNATURE ||
                VirtualQuery(modules[i], &region, sizeof(region)) != sizeof(region) ||
                region.AllocationBase != modules[i] || region.Type != MEM_IMAGE) {
                std::printf("FAIL original_mapped_view=%ls pass=%d\n", names[i], pass);
                return 1;
            }
        }
    }
    for (size_t i = 8; i; --i) FreeLibrary(modules[i - 1]);
    std::printf("SUMMARY mapped_installer bits=%zu passes=2 original_views=8 failures=0\n", sizeof(void*) * 8);
    return 0;
}
