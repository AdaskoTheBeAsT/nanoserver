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
#include <vector>
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

void CheckWideGdiExports(HDC dc) {
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    auto abc = gdi ? reinterpret_cast<decltype(&GetCharABCWidthsFloatW)>(
        GetProcAddress(gdi, "GetCharABCWidthsFloatW")) : nullptr;
    auto reset = gdi ? reinterpret_cast<decltype(&ResetDCW)>(GetProcAddress(gdi, "ResetDCW")) : nullptr;
    Check("gdi32_exports_abc_widths_w", abc != nullptr);
    Check("gdi32_exports_reset_dc_w", reset != nullptr);

    if (abc) {
        ABCFLOAT widths[4] = {{}, {}, {}, {1.0f, 2.0f, 3.0f}};
        SIZE extent{};
        bool measured = abc(dc, 0x03b1, 0x03b3, widths) &&
            GetTextExtentPoint32W(dc, L"\u03b1\u03b2\u03b3", 3, &extent);
        for (int i = 0; i < 3; ++i) {
            measured = measured && widths[i].abcfA == 0.0f && widths[i].abcfB == 8.0f && widths[i].abcfC == 0.0f;
        }
        Check("abc_widths_inclusive_and_match_extent", measured && extent.cx == 24 &&
            widths[3].abcfA == 1.0f && widths[3].abcfB == 2.0f && widths[3].abcfC == 3.0f);
        HDC displayDc = GetDC(nullptr);
        Check("abc_widths_single_last_code_unit", abc(displayDc, MAXWORD, MAXWORD, widths) &&
            widths[0].abcfB == 8.0f);
        if (displayDc) ReleaseDC(nullptr, displayDc);
        SetLastError(ERROR_SUCCESS);
        Check("abc_widths_invalid_dc", !abc(nullptr, 'A', 'A', widths) &&
            GetLastError() == ERROR_INVALID_HANDLE);
        SetLastError(ERROR_SUCCESS);
        Check("abc_widths_null_output", !abc(dc, 'A', 'A', nullptr) &&
            GetLastError() == ERROR_INVALID_PARAMETER);
        SetLastError(ERROR_SUCCESS);
        Check("abc_widths_reversed_range", !abc(dc, 'B', 'A', widths + 3) &&
            GetLastError() == ERROR_INVALID_PARAMETER && widths[3].abcfB == 2.0f);
        SetLastError(ERROR_SUCCESS);
        Check("abc_widths_out_of_range", !abc(dc, 0, MAXDWORD, widths + 3) &&
            GetLastError() == ERROR_INVALID_PARAMETER && widths[3].abcfB == 2.0f);
    }

    if (reset) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        mode.dmFields = DM_ORIENTATION;
        mode.dmOrientation = DMORIENT_LANDSCAPE;
        Check("reset_dc_preserves_headless_dc", dc && reset(dc, nullptr) == dc);
        SetLastError(ERROR_SUCCESS);
        Check("reset_dc_rejects_printer_settings", !reset(dc, &mode) &&
            GetLastError() == ERROR_NOT_SUPPORTED);
        SetLastError(ERROR_SUCCESS);
        Check("reset_dc_rejects_unknown_handle",
            !reset(reinterpret_cast<HDC>(static_cast<ULONG_PTR>(1)), nullptr) &&
            GetLastError() == ERROR_INVALID_HANDLE);
    }
}

int CALLBACK CountTrueType(const LOGFONTW* font, const TEXTMETRICW* tm, DWORD type, LPARAM count) {
    if (!_wcsicmp(font->lfFaceName, L"Lucida Console") &&
        (tm->tmPitchAndFamily & TMPF_TRUETYPE) && (type & TRUETYPE_FONTTYPE)) {
        ++*reinterpret_cast<int*>(count);
    }
    return 1;
}

int CheckHeadlessFonts() {
    HDC dc = CreateCompatibleDC(nullptr);
    HDC other = CreateCompatibleDC(nullptr);
    Check("font_distinct_dcs", dc && other && dc != other);
    if (!dc || !other) return 1;
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    int count = 0;
    EnumFontFamiliesExW(dc, &request, CountTrueType, reinterpret_cast<LPARAM>(&count), 0);
    Check("font_enumerates_installed_face", count > 0);
    wcscpy_s(request.lfFaceName, L"Unavailable font");
    count = 0;
    EnumFontFamiliesExW(dc, &request, CountTrueType, reinterpret_cast<LPARAM>(&count), 0);
    Check("font_filter_is_respected", !count);
    wcscpy_s(request.lfFaceName, L"Lucida Console");
    request.lfHeight = -16;
    HFONT smallFont = CreateFontIndirectW(&request);
    request.lfHeight = -32;
    HFONT large = CreateFontIndirectW(&request);
    HGDIOBJ previous = smallFont ? SelectObject(dc, smallFont) : nullptr;
    HGDIOBJ otherPrevious = large ? SelectObject(other, large) : nullptr;
    Check("font_selection_is_per_dc", smallFont && large && smallFont != large && previous && otherPrevious &&
        GetCurrentObject(dc, OBJ_FONT) == smallFont && GetCurrentObject(other, OBJ_FONT) == large);
    TEXTMETRICW smallMetrics{}, largeMetrics{};
    Check("font_metrics_scale", GetTextMetricsW(dc, &smallMetrics) &&
        GetTextMetricsW(other, &largeMetrics) && smallMetrics.tmHeight > 0 &&
        largeMetrics.tmHeight > smallMetrics.tmHeight && largeMetrics.tmAveCharWidth > smallMetrics.tmAveCharWidth);
    wchar_t name[LF_FACESIZE]{};
    Check("font_face_is_reported", GetTextFaceW(dc, ARRAYSIZE(name), name) &&
        !_wcsicmp(name, L"Lucida Console"));
    BYTE head[5]{0, 0, 0, 0, 0x55};
    Check("font_file_data_and_buffer_bounds", GetFontData(dc, 0, 0, nullptr, 0) > sizeof(head) &&
        GetFontData(dc, 0, 0, head, 4) == 4 &&
        head[0] == 0 && head[1] == 1 && head[2] == 0 && head[3] == 0 && head[4] == 0x55 &&
        GetFontData(dc, MAXDWORD, 0, nullptr, 0) == GDI_ERROR);
    UINT required = GetOutlineTextMetricsW(dc, 0, nullptr);
    std::vector<BYTE> metrics(required);
    auto* outline = reinterpret_cast<OUTLINETEXTMETRICW*>(metrics.data());
    Check("font_outline_metrics_are_sized", required > sizeof(OUTLINETEXTMETRICW) &&
        GetOutlineTextMetricsW(dc, required, outline) == required && outline->otmEMSquare > 0);
    MAT2 matrix{};
    matrix.eM11.value = matrix.eM22.value = 1;
    GLYPHMETRICS gm{};
    DWORD size = GetGlyphOutlineW(dc, L'A', GGO_NATIVE, &gm, 0, nullptr, &matrix);
    bool hasOutline = size > sizeof(TTPOLYGONHEADER) && size < 65536;
    Check("font_glyph_outline_size", hasOutline && gm.gmBlackBoxX > 0 && gm.gmCellIncX > 0);
    if (hasOutline) {
        std::vector<BYTE> glyph(size + 1, 0x55);
        Check("font_glyph_outline_bytes", GetGlyphOutlineW(dc, L'A', GGO_NATIVE, &gm, size,
            glyph.data(), &matrix) == size && glyph[size] == 0x55 &&
            reinterpret_cast<const TTPOLYGONHEADER*>(glyph.data())->dwType == TT_POLYGON_TYPE);
        Check("font_glyph_outline_short_buffer", GetGlyphOutlineW(dc, L'A', GGO_NATIVE, &gm,
            size - 1, glyph.data(), &matrix) == GDI_ERROR && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    }
    DWORD composite = GetGlyphOutlineW(dc, L'\u00e9', GGO_NATIVE, &gm, 0, nullptr, &matrix);
    Check("font_composite_glyph_outline", composite > sizeof(TTPOLYGONHEADER) &&
        composite < 65536 && gm.gmBlackBoxY > 0);
    auto width = reinterpret_cast<decltype(&GetCharWidthI)>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "GetCharWidthI"));
    int advance = 0;
    Check("font_qt_glyph_width_export", width && width(dc, 1, 1, nullptr, &advance) && advance > 0);
    Check("font_selected_object_not_deleted", smallFont && !DeleteObject(smallFont));
    if (previous && otherPrevious) {
        Check("font_restore_and_delete", SelectObject(dc, previous) == smallFont &&
            SelectObject(other, otherPrevious) == large && DeleteObject(smallFont) && DeleteObject(large));
    }
    DeleteDC(other);
    DeleteDC(dc);
    std::printf("SUMMARY headless font failures=%d\n", failures);
    return failures ? 1 : 0;
}

int CALLBACK EnumerateArial(const LOGFONTW* font, const TEXTMETRICW* tm, DWORD type, LPARAM context) {
    if (_wcsicmp(font->lfFaceName, L"Arial") || !(type & TRUETYPE_FONTTYPE)) return 1;
    auto& styles = *reinterpret_cast<unsigned*>(context);
    if (tm->tmItalic) styles |= 4;
    else if (tm->tmWeight >= FW_BOLD) styles |= 2;
    else styles |= 1;
    return 1;
}

int CALLBACK CountAndStop(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM data) {
    ++*reinterpret_cast<unsigned*>(data);
    return 0;
}

bool FontDataMatchesFile(HDC dc, const wchar_t* filename) {
    wchar_t path[MAX_PATH];
    UINT length = GetWindowsDirectoryW(path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path) ||
        wcscat_s(path, L"\\Fonts\\") || wcscat_s(path, filename)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(file, nullptr);
    bool matched = size && size <= 32 * 1024 * 1024 && GetFontData(dc, 0, 0, nullptr, 0) == size;
    if (matched) {
        std::vector<BYTE> expected(size), actual(size);
        DWORD read = 0;
        matched = ReadFile(file, expected.data(), size, &read, nullptr) && read == size &&
            GetFontData(dc, 0, 0, actual.data(), size) == size && actual == expected;
    }
    CloseHandle(file);
    return matched;
}

// This mode needs the fixture fonts registered in the container before startup.
int CheckInstalledFontSelection() {
    HDC dc = CreateCompatibleDC(nullptr), other = CreateCompatibleDC(nullptr);
    if (!dc || !other) return 1;
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(request.lfFaceName, L"Arial");
    unsigned styles = 0;
    EnumFontFamiliesExW(dc, &request, EnumerateArial, reinterpret_cast<LPARAM>(&styles), 0);
    Check("registered_family_enumerates_all_styles", styles == 7);
    unsigned callbacks = 0;
    Check("registered_font_enumeration_stops_on_callback_request",
        !EnumFontFamiliesExW(dc, &request, CountAndStop, reinterpret_cast<LPARAM>(&callbacks), 0) &&
        callbacks == 1);
    auto matches = [&](const wchar_t* requested, const wchar_t* family, LONG weight, BYTE italic,
                       const wchar_t* file) {
        wcscpy_s(request.lfFaceName, requested);
        request.lfHeight = -18;
        request.lfWeight = weight;
        request.lfItalic = italic;
        HFONT font = CreateFontIndirectW(&request);
        if (!font) return false;
        HGDIOBJ old = SelectObject(dc, font);
        wchar_t name[LF_FACESIZE]{};
        TEXTMETRICW metrics{};
        const bool result = old && GetTextFaceW(dc, ARRAYSIZE(name), name) && !_wcsicmp(name, family) &&
            GetTextMetricsW(dc, &metrics) && metrics.tmWeight == weight && bool(metrics.tmItalic) == bool(italic) &&
            FontDataMatchesFile(dc, file) && FontDataMatchesFile(other, L"lucon.ttf");
        const bool restored = old && SelectObject(dc, old) == font;
        return DeleteObject(font) && restored && result;
    };
    Check("regular_face_matched_case_insensitively", matches(L"aRiAl", L"Arial", FW_NORMAL, 0, L"arial.ttf"));
    Check("bold_face_uses_registered_file", matches(L"Arial", L"Arial", FW_BOLD, 0, L"arialbd.ttf"));
    Check("italic_face_uses_registered_file", matches(L"Arial", L"Arial", FW_NORMAL, 1, L"ariali.ttf"));
    Check("different_family_uses_registered_file", matches(L"Times New Roman", L"Times New Roman",
        FW_NORMAL, 0, L"times.ttf"));
    Check("missing_family_uses_stock_face", matches(L"Uninstalled Family", L"Lucida Console",
        FW_NORMAL, 0, L"lucon.ttf"));
    DeleteDC(other);
    DeleteDC(dc);
    std::printf("SUMMARY installed font failures=%d\n", failures);
    return failures ? 1 : 0;
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
    if (argc == 2 && !std::strcmp(argv[1], "--headless-fonts")) return CheckHeadlessFonts();
    if (argc == 2 && !std::strcmp(argv[1], "--installed-fonts")) return CheckInstalledFontSelection();
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

    HDC dc = CreateCompatibleDC(nullptr);
    CheckWideGdiExports(dc);

    // Presence is not a shaping guarantee. Record the existing companion
    // implementation's response without advertising it as a text capability.
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
