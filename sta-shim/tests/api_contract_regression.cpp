// Boundary probes run as ContainerUser. The explicit IIS fault-injection mode
// runs only inside a qualification container as ContainerAdministrator.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <winsvc.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#pragma comment(lib, "advapi32.lib")

namespace {
int failures = 0;
void Check(const char* name, bool passed) {
    std::printf("CHECK %s %s error=%lu\n", name, passed ? "PASS" : "FAIL", GetLastError());
    if (!passed) ++failures;
}

bool RewrittenImageExtentsMatchHeaders() {
    bool matched = true;
    const wchar_t* names[] = {L"kernel32.dll", L"user32.dll", L"ole32.dll", L"gdi32.dll",
        L"comctl32.dll", L"shell32.dll", L"winmm.dll", L"shlwapi.dll"};
    for (const wchar_t* name : names) {
        HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        DWORD declared = 0;
        SIZE_T extent = 0;
        if (module) {
            const auto* base = reinterpret_cast<const BYTE*>(module);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE)
                declared = nt->OptionalHeader.SizeOfImage;
            const BYTE* cursor = base;
            MEMORY_BASIC_INFORMATION region{};
            while (VirtualQuery(cursor, &region, sizeof(region)) == sizeof(region) &&
                    region.AllocationBase == module) {
                if (region.RegionSize > SIZE_MAX - extent) { extent = SIZE_MAX; break; }
                extent += region.RegionSize;
                cursor = static_cast<const BYTE*>(region.BaseAddress) + region.RegionSize;
            }
        }
        bool equal = module && declared && extent == declared;
        std::printf("OBSERVE rewritten_image=%ls declared=%lu allocation=%zu matched=%d\n",
            name, declared, extent, equal);
        matched = matched && equal;
        if (module) FreeLibrary(module);
    }
    return matched;
}

int TerminateWasHost() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    SC_HANDLE service = manager ? OpenServiceW(manager, L"WAS", SERVICE_QUERY_STATUS) : nullptr;
    SERVICE_STATUS_PROCESS status{};
    DWORD needed = 0;
    bool running = service && QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed) &&
        status.dwCurrentState == SERVICE_RUNNING && status.dwProcessId != 0;
    Check("iis_was_service_running", running);
    if (service) CloseServiceHandle(service);
    if (manager) CloseServiceHandle(manager);
    HANDLE process = running ? OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, status.dwProcessId) : nullptr;
    Check("iis_was_process_opened", process != nullptr);
    wchar_t path[MAX_PATH]{};
    DWORD length = MAX_PATH;
    bool identified = process && QueryFullProcessImageNameW(process, 0, path, &length);
    const wchar_t* name = identified ? std::wcsrchr(path, L'\\') : nullptr;
    bool host = name && !_wcsicmp(name + 1, L"svchost.exe");
    Check("iis_was_host_identified", host);
    bool terminated = host && TerminateProcess(process, 1);
    Check("iis_was_host_terminated", terminated);
    if (process) CloseHandle(process);
    return terminated ? 0 : 1;
}
}

int main(int argc, char** argv) {
    GetDesktopWindow();
    HMODULE shim = GetModuleHandleW(L"nanoserver_sta_shim.dll");
    using ActiveFn = int (WINAPI*)();
    auto active = shim ? reinterpret_cast<ActiveFn>(GetProcAddress(shim, "NanoServerStaShim_IsActive")) : nullptr;
    if (!active || !active()) {
        std::puts("FATAL this contract probe requires the active Nano provider");
        return 64;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--terminate-was-host")) {
        FILE* log = nullptr;
        if (freopen_s(&log, "C:\\Temp\\native-was-loss.log", "w", stdout)) return 1;
        setvbuf(stdout, nullptr, _IONBF, 0);
        return TerminateWasHost();
    }
    if (argc != 1) return 64;
    Check("rewritten_images_have_no_extra_reservation", RewrittenImageExtentsMatchHeaders());
    wchar_t directory[MAX_PATH]{}, file[MAX_PATH]{}, canonical[MAX_PATH]{};
    bool created = GetTempPathW(MAX_PATH, directory) && GetTempFileNameW(directory, L"nac", 0, file);
    Check("temp_created_and_canonical", created && !_wcsicmp(directory, L"C:\\Temp\\") &&
        GetLongPathNameW(file, canonical, MAX_PATH));
    WIN32_FIND_DATAW found{};
    HANDLE search = created ? FindFirstFileW(file, &found) : INVALID_HANDLE_VALUE;
    Check("temp_find_first_file", search != INVALID_HANDLE_VALUE);
    if (search != INVALID_HANDLE_VALUE) FindClose(search);
    char ansi[MAX_PATH]{}, longAnsi[MAX_PATH]{};
    Check("temp_ansi_canonical", created &&
        WideCharToMultiByte(CP_ACP, 0, file, -1, ansi, MAX_PATH, nullptr, nullptr) &&
        GetLongPathNameA(ansi, longAnsi, MAX_PATH));
    if (created) DeleteFileW(file);

    char bounded[3] = {'a', 'b', 'c'};
    SIZE size{}; int fit = -1;
    int advances[4] = {-1, -1, -1, 0x76543210};
    Check("extent_below_exact_boundary", GetTextExtentExPointA(nullptr, bounded, 3, 23,
        &fit, advances, &size) && fit == 2 && advances[1] == 16 &&
        advances[2] == -1 && advances[3] == 0x76543210);
    Check("extent_exact_boundary", GetTextExtentExPointA(nullptr, bounded, 3, 24,
        &fit, advances, &size) && fit == 3 && advances[2] == 24 && advances[3] == 0x76543210);
    SetLastError(ERROR_SUCCESS);
    Check("extent_null_size_rejected", !GetTextExtentExPointA(nullptr, bounded, 3, 24,
        nullptr, nullptr, nullptr) && GetLastError() == ERROR_INVALID_PARAMETER);

    HMODULE olepro = LoadLibraryExW(L"olepro32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using FrameFn = HRESULT (WINAPI*)(HWND, UINT, UINT, LPCOLESTR, ULONG, LPUNKNOWN*, ULONG, LPCLSID, LCID, DWORD, LPVOID);
    using IndirectFn = HRESULT (WINAPI*)(LPVOID);
    auto frame = olepro ? reinterpret_cast<FrameFn>(GetProcAddress(olepro, "OleCreatePropertyFrame")) : nullptr;
    auto indirect = olepro ? reinterpret_cast<IndirectFn>(GetProcAddress(olepro, "OleCreatePropertyFrameIndirect")) : nullptr;
    Check("property_frame_honestly_unsupported", frame &&
        frame(nullptr, 0, 0, nullptr, 0, nullptr, 0, nullptr, 0, 0, nullptr) == E_NOTIMPL);
    Check("property_frame_indirect_honestly_unsupported", indirect && indirect(nullptr) == E_NOTIMPL);
    if (olepro) FreeLibrary(olepro);

    // Presence is not a shaping guarantee. Record the existing companion
    // implementation's response without advertising it as a text capability.
    HDC dc = CreateCompatibleDC(nullptr);
    GCP_RESULTSA placement{sizeof(placement)};
    char placed[4]{}, classes[3]{};
    UINT order[3]{}; int widths[3]{}, caret[3]{}; wchar_t glyphs[3]{};
    placement.lpOutString = placed; placement.lpOrder = order; placement.lpDx = widths;
    placement.lpCaretPos = caret; placement.lpClass = classes; placement.lpGlyphs = glyphs;
    placement.nGlyphs = 3;
    SetLastError(ERROR_SUCCESS);
    DWORD layout = GetCharacterPlacementA(dc, bounded, 3, 100, &placement, 0);
    std::printf("OBSERVE existing_character_placement_a=%lu error=%lu\n", layout, GetLastError());
    GCP_RESULTSW widePlacement{sizeof(widePlacement)};
    wchar_t widePlaced[4]{};
    widePlacement.lpOutString = widePlaced; widePlacement.lpOrder = order; widePlacement.lpDx = widths;
    widePlacement.lpCaretPos = caret; widePlacement.lpClass = classes; widePlacement.lpGlyphs = glyphs;
    widePlacement.nGlyphs = 3;
    SetLastError(ERROR_SUCCESS);
    layout = GetCharacterPlacementW(dc, L"abc", 3, 100, &widePlacement, 0);
    std::printf("OBSERVE existing_character_placement_w=%lu error=%lu\n", layout, GetLastError());
    GLYPHMETRICS metrics{}; MAT2 transform{};
    transform.eM11.value = transform.eM22.value = 1;
    SetLastError(ERROR_SUCCESS);
    DWORD outline = GetGlyphOutlineA(dc, 'A', GGO_METRICS, &metrics, 0, nullptr, &transform);
    std::printf("OBSERVE existing_glyph_outline_a=%lu error=%lu\n", outline, GetLastError());
    SetLastError(ERROR_SUCCESS);
    outline = GetGlyphOutlineW(dc, L'A', GGO_METRICS, &metrics, 0, nullptr, &transform);
    std::printf("OBSERVE existing_glyph_outline_w=%lu error=%lu\n", outline, GetLastError());
    DeleteDC(dc);
    std::printf("SUMMARY API contract failures=%d\n", failures);
    return failures ? 1 : 0;
}
