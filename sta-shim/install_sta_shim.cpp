// install_sta_shim.cpp
// Standalone installer tool that patches PE import tables of combase.dll, user32.dll, and ole32.dll
// on Windows Nano Server so nanoserver_sta_shim.dll is loaded automatically into processes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>

namespace {

DWORD RvaToOffset(IMAGE_NT_HEADERS* nt, WORD numSections, DWORD rva) {
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < numSections; ++i) {
        DWORD secSize = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData
                            ? sec[i].Misc.VirtualSize
                            : sec[i].SizeOfRawData;
        if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + secSize) {
            return sec[i].PointerToRawData + (rva - sec[i].VirtualAddress);
        }
    }
    return 0;
}

DWORD ComputePeChecksum(const BYTE* data, size_t size, size_t checksumOffset) {
    unsigned long long sum = 0;
    size_t numWords = size / 2;
    const WORD* words = reinterpret_cast<const WORD*>(data);
    for (size_t i = 0; i < numWords; ++i) {
        size_t bytePos = i * 2;
        if (bytePos == checksumOffset || bytePos == checksumOffset + 2) {
            continue;
        }
        sum += words[i];
        sum = (sum & 0xFFFFULL) + (sum >> 16);
    }
    if (size & 1) {
        sum += data[size - 1];
        sum = (sum & 0xFFFFULL) + (sum >> 16);
    }
    sum = (sum & 0xFFFFULL) + (sum >> 16);
    return static_cast<DWORD>(sum + size);
}

bool PatchPeFileImports(const wchar_t* filePath) {
    HANDLE hFile = CreateFileW(
        filePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        wprintf(L"[SKIP] File not found: %s\n", filePath);
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize < 512) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<BYTE> buf(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buf.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    WORD magic = *reinterpret_cast<WORD*>(&nt->OptionalHeader.Magic);
    DWORD impRva = 0;
    size_t checksumOffset = static_cast<size_t>(dos->e_lfanew) + 24 + 64;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        auto* nt32 = reinterpret_cast<IMAGE_NT_HEADERS32*>(nt);
        impRva = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        // Strip IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY if set
        nt32->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
        // Clear security directory (Authenticode signature table) since bytes are modified
        nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress = 0;
        nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size = 0;
    } else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        auto* nt64 = reinterpret_cast<IMAGE_NT_HEADERS64*>(nt);
        impRva = nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        nt64->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
        nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress = 0;
        nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size = 0;
    } else {
        return false;
    }

    if (impRva == 0) {
        return false;
    }

    DWORD impOff = RvaToOffset(nt, nt->FileHeader.NumberOfSections, impRva);
    if (impOff == 0) {
        return false;
    }

    bool modified = false;
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(buf.data() + impOff);
    for (; desc->Name != 0 || desc->FirstThunk != 0; ++desc) {
        DWORD nameOff = RvaToOffset(nt, nt->FileHeader.NumberOfSections, desc->Name);
        if (nameOff == 0 || nameOff + 36 > buf.size()) {
            continue;
        }
        char* dllName = reinterpret_cast<char*>(buf.data() + nameOff);
        if (_stricmp(dllName, "api-ms-win-core-apiquery-l1-1-0.dll") == 0 ||
            _stricmp(dllName, "api-ms-win-core-apiquery-l1-1-1.dll") == 0 ||
            _stricmp(dllName, "nanoserver_sta_shim.dll") == 0) {
            const char* replacement = "nanoserver_sta_shim.dll";
            size_t origLen = strlen(dllName);
            size_t repLen = strlen(replacement);
            memset(dllName, 0, origLen + 1);
            memcpy(dllName, replacement, repLen + 1);
            modified = true;
        }
    }

    if (!modified) {
        wprintf(L"[WARN] No target import found in: %s\n", filePath);
        return false;
    }

    DWORD newChecksum = ComputePeChecksum(buf.data(), buf.size(), checksumOffset);
    *reinterpret_cast<DWORD*>(buf.data() + checksumOffset) = newChecksum;

    SetFileAttributesW(filePath, FILE_ATTRIBUTE_NORMAL);
    HANDLE hDirect = CreateFileW(
        filePath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (hDirect != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        BOOL writeOk = WriteFile(hDirect, buf.data(), static_cast<DWORD>(buf.size()), &bytesWritten, nullptr);
        CloseHandle(hDirect);
        if (writeOk && bytesWritten == buf.size()) {
            wprintf(L"[OK] Patched imports in: %s\n", filePath);
            return true;
        }
    }

    std::wstring tmpPath = std::wstring(filePath) + L".new";
    std::wstring bakPath = std::wstring(filePath) + L".orig";

    HANDLE hOut = CreateFileW(
        tmpPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (hOut == INVALID_HANDLE_VALUE) {
        wprintf(L"[ERR] Cannot create temp file: %s (err=%lu)\n", tmpPath.c_str(), GetLastError());
        return false;
    }
    DWORD bytesWritten = 0;
    BOOL writeOk = WriteFile(hOut, buf.data(), static_cast<DWORD>(buf.size()), &bytesWritten, nullptr);
    CloseHandle(hOut);
    if (!writeOk || bytesWritten != buf.size()) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    // Save backup if not already saved, or move current file aside if locked
    DeleteFileW(bakPath.c_str());
    if (!MoveFileExW(filePath, bakPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        wprintf(L"[ERR] Cannot move original: %s (err=%lu)\n", filePath, GetLastError());
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    if (!MoveFileExW(tmpPath.c_str(), filePath, MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(bakPath.c_str(), filePath, MOVEFILE_REPLACE_EXISTING);
        wprintf(L"[ERR] Cannot replace with patched: %s (err=%lu)\n", filePath, GetLastError());
        return false;
    }
    // Try to remove .orig (if not in use)
    DeleteFileW(bakPath.c_str());

    wprintf(L"[OK] Patched imports in: %s\n", filePath);
    return true;
}

void EnablePrivilege(LPCWSTR privName) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return;
    }
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(nullptr, privName, &tp.Privileges[0].Luid)) {
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(hToken);
}

void RemoveKnownDllEntries() {
    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\KnownDLLs",
        REG_OPTION_BACKUP_RESTORE,
        KEY_SET_VALUE,
        &hKey);
    if (status == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"user32");
        RegDeleteValueW(hKey, L"ole32");
        RegCloseKey(hKey);
        wprintf(L"[OK] Updated KnownDLLs registry key.\n");
    } else {
        wprintf(L"[WARN] Could not open KnownDLLs registry key (status=%ld).\n", status);
    }
}

void CopyForwardersIfMissing(const wchar_t* sysDir) {
    std::wstring pattern = std::wstring(sysDir) + L"\\forwarders\\*.dll";
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    int copied = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        std::wstring src = std::wstring(sysDir) + L"\\forwarders\\" + fd.cFileName;
        std::wstring dst = std::wstring(sysDir) + L"\\" + fd.cFileName;
        if (CopyFileW(src.c_str(), dst.c_str(), TRUE)) {
            ++copied;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    wprintf(L"[OK] Copied %d forwarder DLLs into %s\n", copied, sysDir);
}

void CopyShimDll(const wchar_t* srcShim, const wchar_t* sysDir) {
    std::wstring dstMain = std::wstring(sysDir) + L"\\nanoserver_sta_shim.dll";
    std::wstring dstFwd = std::wstring(sysDir) + L"\\forwarders\\nanoserver_sta_shim.dll";
    if (CopyFileW(srcShim, dstMain.c_str(), FALSE)) {
        wprintf(L"[OK] Installed %s\n", dstMain.c_str());
    }
    CopyFileW(srcShim, dstFwd.c_str(), FALSE);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (GetFileAttributesW(L"C:\\Windows\\System32\\win32u.dll") != INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"[ABORT] win32u.dll detected in System32. Refusing to run on a non-Nano-Server host OS.\n");
        return 1;
    }

    EnablePrivilege(L"SeBackupPrivilege");
    EnablePrivilege(L"SeRestorePrivilege");
    EnablePrivilege(L"SeTakeOwnershipPrivilege");

    wchar_t exeDir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }
    const wchar_t* baseDir = (argc > 1) ? argv[1] : exeDir;

    std::wstring shim64 = std::wstring(baseDir) + L"\\nanoserver_sta_shim_x64.dll";
    std::wstring shim86 = std::wstring(baseDir) + L"\\nanoserver_sta_shim_x86.dll";

    RemoveKnownDllEntries();

    const wchar_t* sys32 = L"C:\\Windows\\System32";
    if (GetFileAttributesW(sys32) != INVALID_FILE_ATTRIBUTES) {
        CopyShimDll(shim64.c_str(), sys32);
        PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\user32.dll");
        PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\ole32.dll");
        CopyFileW(L"C:\\Windows\\System32\\forwarders\\user32.dll", L"C:\\Windows\\System32\\user32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\System32\\forwarders\\ole32.dll", L"C:\\Windows\\System32\\ole32.dll", FALSE);
    }

    const wchar_t* sysWow64 = L"C:\\Windows\\SysWOW64";
    const wchar_t* sysWow64Fwd = L"C:\\Windows\\SysWOW64\\forwarders";
    if (GetFileAttributesW(sysWow64Fwd) != INVALID_FILE_ATTRIBUTES) {
        CopyShimDll(shim86.c_str(), sysWow64);
        PatchPeFileImports(L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll");
        PatchPeFileImports(L"C:\\Windows\\SysWOW64\\forwarders\\ole32.dll");
        CopyFileW(L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll", L"C:\\Windows\\SysWOW64\\user32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\SysWOW64\\forwarders\\ole32.dll", L"C:\\Windows\\SysWOW64\\ole32.dll", FALSE);
    }

    return 0;
}
