// Ordinary kernel32 imports, including a late-loaded DllMain fixture.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
void Check(const char* name, bool success) {
    std::printf("CHECK %s %s error=%lu\n", name, success ? "PASS" : "FAIL", GetLastError());
    std::fflush(stdout);
    if (!success) ++failures;
}
}

int wmain(int argc, wchar_t* argv[]) {
    // An existing shimmed system import supplies the native provider before
    // clients' initializers. Do not require applications to call a new API.
    GetDesktopWindow();
    HMODULE explicitProvider = nullptr;
    if (argc > 1 && wcscmp(argv[1], L"--load-provider") == 0) {
        std::wstring originalName = L"NanoOsOwnedAtom_" + std::to_wstring(GetCurrentProcessId());
        ATOM original = GlobalAddAtomW(originalName.c_str());
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        FARPROC originalSxs = GetProcAddress(kernel, "FindActCtxSectionStringW");
        wchar_t executablePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        wchar_t* separator = wcsrchr(executablePath, L'\\');
        if (separator) separator[1] = L'\0';
        std::wstring library = std::wstring(executablePath) + (sizeof(void*) == 4 ?
            L"nanoserver_sta_shim_x86.dll" : L"nanoserver_sta_shim_x64.dll");
        explicitProvider = LoadLibraryW(library.c_str());
        Check("os_control_provider_loaded", explicitProvider != nullptr);
        Check("functional_sxs_lookup_preserved", originalSxs && originalSxs ==
            GetProcAddress(kernel, "FindActCtxSectionStringW"));
        wchar_t originalOutput[256]{};
        Check("preexisting_os_atom_preserved", original &&
            GlobalFindAtomW(originalName.c_str()) == original &&
            GlobalGetAtomNameW(original, originalOutput, 256) == originalName.size() &&
            originalName == originalOutput);
        if (original) GlobalDeleteAtom(original);
    }
    auto address = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GlobalAddAtomW");
    HMODULE provider = nullptr;
    wchar_t path[MAX_PATH]{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &provider);
    GetModuleFileNameW(provider, path, MAX_PATH);
    std::wprintf(L"GlobalAddAtomW provider=%s address=%p\n", path, address);
    SetLastError(ERROR_SUCCESS);
    ATOM probe = GlobalAddAtomW(L"NanoAtomPermissionProbe");
    std::printf("OBSERVE GlobalAddAtomW=%u error=%lu\n", probe, GetLastError());
    if (probe && argc > 1 && wcscmp(argv[1], L"--native-query-probe") == 0) {
        using QueryFn = LONG (NTAPI*)(ATOM, ULONG, PVOID, ULONG, PULONG);
        auto query = reinterpret_cast<QueryFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationAtom"));
        for (ULONG length : {0ul, 6ul, 8ul, 10ul, 12ul, 64ul}) {
            USHORT buffer[64];
            memset(buffer, 0xEF, sizeof(buffer));
            ULONG required = 0;
            LONG status = query(probe, 0, buffer, length, &required);
            std::printf("OBSERVE NtQueryAtom length=%lu status=%08lX required=%lu header=%04x,%04x,%04x name=%04x,%04x,%04x,%04x\n",
                length, status, required, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6]);
        }
        GlobalDeleteAtom(probe);
        return 0;
    }
    if (probe) GlobalDeleteAtom(probe);
    if (argc > 1 && wcscmp(argv[1], L"--probe") == 0) return 0;
    Check("global_atom_available", probe != 0);
    if (!probe) return 1;

    std::wstring name = L"NanoAtom_" + std::to_wstring(GetCurrentProcessId());
    using DynamicAddFn = ATOM (WINAPI*)(LPCWSTR);
    using DynamicFindFn = ATOM (WINAPI*)(LPCWSTR);
    auto dynamicAdd = reinterpret_cast<DynamicAddFn>(address);
    auto dynamicFind = reinterpret_cast<DynamicFindFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GlobalFindAtomW"));
    ATOM dynamicAtom = dynamicAdd(name.c_str());
    Check("dynamic_lookup_invocation", dynamicAtom && dynamicFind && dynamicFind(name.c_str()) == dynamicAtom);
    if (dynamicAtom) GlobalDeleteAtom(dynamicAtom);
    ATOM first = GlobalAddAtomW(name.c_str());
    std::wstring lower = name;
    for (auto& character : lower) character = towlower(character);
    ATOM second = GlobalAddAtomW(lower.c_str());
    Check("case_insensitive_reference", first && second == first && GlobalFindAtomW(lower.c_str()) == first);
    wchar_t output[256]{};
    Check("name_round_trip", GlobalGetAtomNameW(first, output, 256) == name.size() && name == output);
    wchar_t small[] = {L'?', L'?', L'?', L'!'};
    UINT copied = GlobalGetAtomNameW(first, small, 3);
    std::printf("OBSERVE atom_name_boundary copied=%u values=%04x,%04x,%04x,%04x error=%lu\n",
        copied, small[0], small[1], small[2], small[3], GetLastError());
    // The Windows global API fills all cch characters, without a terminating
    // NUL when truncated. This differs from the process-local GetAtomName API.
    Check("name_bounded", copied == 3 && small[0] == L'N' && small[1] == L'a' &&
        small[2] == L'n' && small[3] == L'!');
    GlobalDeleteAtom(first);
    Check("reference_retained", GlobalFindAtomW(name.c_str()) == second);
    GlobalDeleteAtom(second);
    Check("last_reference_removed", GlobalFindAtomW(name.c_str()) == 0);

    ATOM integer = GlobalAddAtomW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1234)));
    wchar_t integerName[16]{};
    Check("integer_atoms", integer == 1234 && GlobalFindAtomA("#1234") == 1234 &&
        GlobalGetAtomNameW(integer, integerName, 16) == 5 && wcscmp(integerName, L"#1234") == 0 &&
        GlobalDeleteAtom(integer) == 0);
    ATOM ansi = GlobalAddAtomA("NanoAnsiAtom");
    char ansiName[32]{};
    Check("ansi_unicode_identity", ansi && GlobalFindAtomW(L"nanoansiatom") == ansi &&
        GlobalGetAtomNameA(ansi, ansiName, 32) == 12 && strcmp(ansiName, "NanoAnsiAtom") == 0);
    GlobalDeleteAtom(ansi);
    std::wstring longName(256, L'x');
    Check("overlong_rejected", GlobalAddAtomW(longName.c_str()) == 0);

    std::atomic<unsigned> ready{0};
    std::atomic<unsigned> errors{0};
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<std::thread> threads;
    ATOM shared[8]{};
    for (unsigned i = 0; i < 8; ++i) threads.emplace_back([&, i] {
        shared[i] = GlobalAddAtomW(L"NanoConcurrentAtom");
        ++ready;
        WaitForSingleObject(release, 10000);
        if (!shared[i] || GlobalDeleteAtom(shared[i]) != 0) ++errors;
    });
    while (ready.load() != 8) SwitchToThread();
    bool same = shared[0] != 0;
    for (ATOM atom : shared) same = same && atom == shared[0];
    SetEvent(release);
    for (auto& thread : threads) thread.join();
    CloseHandle(release);
    Check("concurrent_references", same && !errors && !GlobalFindAtomW(L"NanoConcurrentAtom"));

    HMODULE fixture = LoadLibraryW(sizeof(void*) == 4 ? L"atom_fixture_x86.dll" : L"atom_fixture_x64.dll");
    Check("late_dllmain_import", fixture != nullptr);
    if (fixture) FreeLibrary(fixture);
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    wchar_t* slash = wcsrchr(executable, L'\\');
    if (slash) *slash = L'\0';
    std::wstring staticTest = std::wstring(executable) + L"\\AtomStaticRegression.exe";
    if (GetFileAttributesW(staticTest.c_str()) == INVALID_FILE_ATTRIBUTES)
        staticTest = std::wstring(executable) + (sizeof(void*) == 4 ?
            L"\\AtomStaticRegression_x86.exe" : L"\\AtomStaticRegression_x64.exe");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    bool launched = CreateProcessW(staticTest.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
        executable, &startup, &child) != FALSE;
    DWORD code = 1;
    if (launched) {
        if (WaitForSingleObject(child.hProcess, 10000) != WAIT_OBJECT_0) TerminateProcess(child.hProcess, 2);
        GetExitCodeProcess(child.hProcess, &code);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    Check("static_dllmain_import", launched && code == 0);

    HMODULE shim = explicitProvider ? explicitProvider : GetModuleHandleW(L"nanoserver_sta_shim.dll");
    using CapabilitiesFn = DWORD (WINAPI*)(DWORD);
    using ControlFn = void (WINAPI*)();
    auto capabilities = shim ? reinterpret_cast<CapabilitiesFn>(GetProcAddress(shim, "NanoServerStaShim_GetCapabilities")) : nullptr;
    if (capabilities && (capabilities(1) & 0x10)) {
        auto restore = reinterpret_cast<ControlFn>(GetProcAddress(shim, "NanoServerStaShim_RestoreOriginal"));
        auto refresh = reinterpret_cast<ControlFn>(GetProcAddress(shim, "NanoServerStaShim_Refresh"));
        bool unavailableSxs = (capabilities(1) & 0x20) != 0;
        auto lookupSxs = [&] { return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FindActCtxSectionStringW"); };
        Check("sxs_capability_matches_dynamic_probe", unavailableSxs ? !lookupSxs() : lookupSxs() != nullptr);
        ATOM retained = GlobalAddAtomW(L"NanoRestoreRetainedAtom");
        restore();
        Check("atom_restore_disables_capability", capabilities(1) == 0);
        Check("sxs_restore_preserves_original_stub", lookupSxs() != nullptr);
        refresh();
        Check("atom_refresh_preserves_owned_references", retained && GlobalFindAtomW(L"NanoRestoreRetainedAtom") == retained);
        Check("sxs_refresh_reapplies_feature_probe", unavailableSxs ? !lookupSxs() : lookupSxs() != nullptr);
        GlobalDeleteAtom(retained);
        Check("unknown_capability_abi", capabilities(2) == 0);
    }
    if (argc > 1 && wcscmp(argv[1], L"--exhaust") == 0) {
        // Do not exhaust an accessible OS-wide table, even in a control run.
        using AddFn = LONG (NTAPI*)(PWSTR, ULONG, ATOM*, ULONG);
        auto add = reinterpret_cast<AddFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtAddAtomEx"));
        wchar_t guard[] = L"NanoExhaustionGuard";
        ATOM osAtom = 0;
        LONG status = add(guard, sizeof(guard) - sizeof(wchar_t), &osAtom, 0);
        if (status >= 0) {
            using DeleteFn = LONG (NTAPI*)(ATOM);
            reinterpret_cast<DeleteFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtDeleteAtom"))(osAtom);
        }
        bool privateTable = status == static_cast<LONG>(0xC0000022u) && capabilities && (capabilities(1) & 0x10);
        Check("exhaustion_only_private_namespace", privateTable);
        if (privateTable) {
            std::vector<ATOM> held;
            ATOM overflow = 0;
            for (unsigned i = 0; i <= 0x4000; ++i) {
                std::wstring entry = L"NanoExhaustion_" + std::to_wstring(i);
                overflow = GlobalAddAtomW(entry.c_str());
                if (!overflow) break;
                held.push_back(overflow);
            }
            std::printf("OBSERVE private_atom_capacity=%zu error=%lu\n", held.size(), GetLastError());
            Check("private_atom_exhaustion", held.size() == 0x4000 && !overflow);
            for (ATOM entry : held) GlobalDeleteAtom(entry);
            ATOM reused = GlobalAddAtomW(L"NanoExhaustionReused");
            Check("private_atom_capacity_reused", reused != 0);
            if (reused) GlobalDeleteAtom(reused);
        }
    }
    std::printf("SUMMARY failures=%d\n", failures);
    return failures ? 1 : 0;
}
