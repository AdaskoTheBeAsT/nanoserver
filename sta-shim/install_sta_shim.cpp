// install_sta_shim.cpp
// Standalone installer tool that patches PE import tables of combase.dll, user32.dll, and ole32.dll
// on Windows Nano Server so nanoserver_sta_shim.dll is loaded automatically into processes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <string>

namespace {

DWORD AlignUp(DWORD value, DWORD alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

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

struct NamedExportEntry {
    std::string Name;
    WORD OrdinalIndex;
};

struct ForwarderFixup {
    WORD OrdinalIndex;
    std::string Target;
};

bool WritePeBufferToFile(const wchar_t* filePath, std::vector<BYTE>& buf, size_t checksumOffset) {
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
    DeleteFileW(bakPath.c_str());
    return true;
}

bool PatchPeFileExports(const wchar_t* filePath, const std::vector<const char*>& missingExports) {
    if (missingExports.empty()) {
        return true;
    }

    HANDLE hFile = CreateFileW(
        filePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        wprintf(L"[SKIP] File not found for export patch: %s\n", filePath);
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
    DWORD expRva = 0;
    DWORD expSize = 0;
    DWORD secAlign = 0;
    DWORD fileAlign = 0;
    DWORD sizeOfHeaders = 0;
    size_t checksumOffset = static_cast<size_t>(dos->e_lfanew) + 24 + 64;

    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        auto* nt32 = reinterpret_cast<IMAGE_NT_HEADERS32*>(nt);
        expRva = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        expSize = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        secAlign = nt32->OptionalHeader.SectionAlignment;
        fileAlign = nt32->OptionalHeader.FileAlignment;
        sizeOfHeaders = nt32->OptionalHeader.SizeOfHeaders;
    } else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        auto* nt64 = reinterpret_cast<IMAGE_NT_HEADERS64*>(nt);
        expRva = nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        expSize = nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        secAlign = nt64->OptionalHeader.SectionAlignment;
        fileAlign = nt64->OptionalHeader.FileAlignment;
        sizeOfHeaders = nt64->OptionalHeader.SizeOfHeaders;
    } else {
        return false;
    }

    if (expRva == 0 || expSize < sizeof(IMAGE_EXPORT_DIRECTORY)) {
        return false;
    }

    WORD numSections = nt->FileHeader.NumberOfSections;
    DWORD expOff = RvaToOffset(nt, numSections, expRva);
    if (expOff == 0 || expOff + sizeof(IMAGE_EXPORT_DIRECTORY) > buf.size()) {
        return false;
    }

    IMAGE_EXPORT_DIRECTORY origExpDir = *reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(buf.data() + expOff);
    DWORD funcsOff = RvaToOffset(nt, numSections, origExpDir.AddressOfFunctions);
    DWORD namesOff = RvaToOffset(nt, numSections, origExpDir.AddressOfNames);
    DWORD ordsOff = RvaToOffset(nt, numSections, origExpDir.AddressOfNameOrdinals);
    DWORD dllNameOff = RvaToOffset(nt, numSections, origExpDir.Name);
    if (funcsOff == 0 || namesOff == 0 || ordsOff == 0 || dllNameOff == 0) {
        return false;
    }

    std::string dllNameStr(reinterpret_cast<const char*>(buf.data() + dllNameOff));
    const DWORD* origFuncs = reinterpret_cast<const DWORD*>(buf.data() + funcsOff);
    const DWORD* origNames = reinterpret_cast<const DWORD*>(buf.data() + namesOff);
    const WORD* origOrds = reinterpret_cast<const WORD*>(buf.data() + ordsOff);

    std::vector<DWORD> newFunctions(origFuncs, origFuncs + origExpDir.NumberOfFunctions);
    std::vector<NamedExportEntry> namedExports;
    namedExports.reserve(origExpDir.NumberOfNames + missingExports.size());

    for (DWORD i = 0; i < origExpDir.NumberOfNames; ++i) {
        DWORD nameOff = RvaToOffset(nt, numSections, origNames[i]);
        if (nameOff == 0 || nameOff >= buf.size()) {
            return false;
        }
        namedExports.push_back(NamedExportEntry{
            std::string(reinterpret_cast<const char*>(buf.data() + nameOff)),
            origOrds[i]});
    }

    std::vector<ForwarderFixup> forwarderFixups;
    for (DWORD i = 0; i < origExpDir.NumberOfFunctions; ++i) {
        DWORD fnRva = origFuncs[i];
        if (fnRva >= expRva && fnRva < expRva + expSize) {
            DWORD fwdOff = RvaToOffset(nt, numSections, fnRva);
            if (fwdOff != 0 && fwdOff < buf.size()) {
                forwarderFixups.push_back(ForwarderFixup{
                    static_cast<WORD>(i),
                    std::string(reinterpret_cast<const char*>(buf.data() + fwdOff))});
            }
        }
    }

    size_t addedCount = 0;
    for (const char* missingName : missingExports) {
        bool exists = false;
        for (const auto& entry : namedExports) {
            if (entry.Name == missingName) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            WORD newOrdIdx = static_cast<WORD>(newFunctions.size());
            newFunctions.push_back(0);
            namedExports.push_back(NamedExportEntry{std::string(missingName), newOrdIdx});
            forwarderFixups.push_back(ForwarderFixup{
                newOrdIdx,
                std::string("nanoserver_sta_shim.") + missingName});
            ++addedCount;
        }
    }

    if (addedCount == 0) {
        wprintf(L"[OK] Exports already present in: %s\n", filePath);
        return true;
    }

    std::sort(
        namedExports.begin(),
        namedExports.end(),
        [](const NamedExportEntry& a, const NamedExportEntry& b) {
            return strcmp(a.Name.c_str(), b.Name.c_str()) < 0;
        });

    auto* sec = IMAGE_FIRST_SECTION(nt);
    size_t newSecHeaderEnd = static_cast<size_t>(
        reinterpret_cast<BYTE*>(&sec[numSections + 1]) - buf.data());
    if (newSecHeaderEnd > sizeOfHeaders) {
        wprintf(L"[ERR] No space for section header in: %s\n", filePath);
        return false;
    }

    DWORD maxRawEnd = 0;
    DWORD maxVirtEnd = 0;
    for (WORD i = 0; i < numSections; ++i) {
        DWORD rawEnd = sec[i].PointerToRawData + sec[i].SizeOfRawData;
        if (rawEnd > maxRawEnd) {
            maxRawEnd = rawEnd;
        }
        DWORD virtSpan = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData
                             ? sec[i].Misc.VirtualSize
                             : sec[i].SizeOfRawData;
        DWORD virtEnd = sec[i].VirtualAddress + virtSpan;
        if (virtEnd > maxVirtEnd) {
            maxVirtEnd = virtEnd;
        }
    }

    DWORD newSecRawPtr = AlignUp(maxRawEnd, fileAlign);
    DWORD newSecRva = AlignUp(maxVirtEnd, secAlign);

    std::vector<BYTE> edata;
    edata.resize(sizeof(IMAGE_EXPORT_DIRECTORY), 0);

    DWORD addrOfFunctionsOffset = static_cast<DWORD>(edata.size());
    edata.resize(addrOfFunctionsOffset + newFunctions.size() * sizeof(DWORD), 0);

    DWORD addrOfNamesOffset = static_cast<DWORD>(edata.size());
    edata.resize(addrOfNamesOffset + namedExports.size() * sizeof(DWORD), 0);

    DWORD addrOfOrdinalsOffset = static_cast<DWORD>(edata.size());
    edata.resize(addrOfOrdinalsOffset + namedExports.size() * sizeof(WORD), 0);

    DWORD dllNameRva = newSecRva + static_cast<DWORD>(edata.size());
    edata.insert(edata.end(), dllNameStr.begin(), dllNameStr.end());
    edata.push_back(0);

    for (size_t i = 0; i < namedExports.size(); ++i) {
        DWORD nameRva = newSecRva + static_cast<DWORD>(edata.size());
        *reinterpret_cast<DWORD*>(edata.data() + addrOfNamesOffset + i * sizeof(DWORD)) = nameRva;
        *reinterpret_cast<WORD*>(edata.data() + addrOfOrdinalsOffset + i * sizeof(WORD)) =
            namedExports[i].OrdinalIndex;
        edata.insert(edata.end(), namedExports[i].Name.begin(), namedExports[i].Name.end());
        edata.push_back(0);
    }

    for (const auto& fixup : forwarderFixups) {
        DWORD fwdRva = newSecRva + static_cast<DWORD>(edata.size());
        newFunctions[fixup.OrdinalIndex] = fwdRva;
        edata.insert(edata.end(), fixup.Target.begin(), fixup.Target.end());
        edata.push_back(0);
    }

    memcpy(
        edata.data() + addrOfFunctionsOffset,
        newFunctions.data(),
        newFunctions.size() * sizeof(DWORD));

    auto* newExpDir = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(edata.data());
    newExpDir->Characteristics = origExpDir.Characteristics;
    newExpDir->TimeDateStamp = origExpDir.TimeDateStamp;
    newExpDir->MajorVersion = origExpDir.MajorVersion;
    newExpDir->MinorVersion = origExpDir.MinorVersion;
    newExpDir->Name = dllNameRva;
    newExpDir->Base = origExpDir.Base;
    newExpDir->NumberOfFunctions = static_cast<DWORD>(newFunctions.size());
    newExpDir->NumberOfNames = static_cast<DWORD>(namedExports.size());
    newExpDir->AddressOfFunctions = newSecRva + addrOfFunctionsOffset;
    newExpDir->AddressOfNames = newSecRva + addrOfNamesOffset;
    newExpDir->AddressOfNameOrdinals = newSecRva + addrOfOrdinalsOffset;

    DWORD virtualSize = static_cast<DWORD>(edata.size());
    DWORD rawSize = AlignUp(virtualSize, fileAlign);

    buf.resize(newSecRawPtr, 0);
    buf.insert(buf.end(), edata.begin(), edata.end());
    buf.resize(newSecRawPtr + rawSize, 0);

    dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
    nt = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.data() + dos->e_lfanew);
    sec = IMAGE_FIRST_SECTION(nt);

    IMAGE_SECTION_HEADER& newSec = sec[numSections];
    memset(&newSec, 0, sizeof(IMAGE_SECTION_HEADER));
    memcpy(newSec.Name, ".edata2", 7);
    newSec.Misc.VirtualSize = virtualSize;
    newSec.VirtualAddress = newSecRva;
    newSec.SizeOfRawData = rawSize;
    newSec.PointerToRawData = newSecRawPtr;
    newSec.Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

    nt->FileHeader.NumberOfSections = static_cast<WORD>(numSections + 1);
    DWORD newSizeOfImage = AlignUp(newSecRva + virtualSize, secAlign);

    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        auto* nt32 = reinterpret_cast<IMAGE_NT_HEADERS32*>(nt);
        nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = newSecRva;
        nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = virtualSize;
        nt32->OptionalHeader.SizeOfImage = newSizeOfImage;
        nt32->OptionalHeader.SizeOfInitializedData += rawSize;
        nt32->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
        nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress = 0;
        nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size = 0;
    } else {
        auto* nt64 = reinterpret_cast<IMAGE_NT_HEADERS64*>(nt);
        nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = newSecRva;
        nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = virtualSize;
        nt64->OptionalHeader.SizeOfImage = newSizeOfImage;
        nt64->OptionalHeader.SizeOfInitializedData += rawSize;
        nt64->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
        nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress = 0;
        nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size = 0;
    }

    if (!WritePeBufferToFile(filePath, buf, checksumOffset)) {
        return false;
    }

    wprintf(L"[OK] Added %zu missing export(s) to: %s\n", addedCount, filePath);
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
        RegDeleteValueW(hKey, L"gdi32");
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

    const std::vector<const char*> kGdi32MissingExports = {
        "CopyEnhMetaFileA",
        "CreatePenIndirect",
        "GetTextExtentPoint32A",
        "SetWinMetaFileBits",
    };
    const std::vector<const char*> kUser32MissingExports = {
        "CreateIcon",
        "GetKeyNameTextA",
    };
    const std::vector<const char*> kComctl32MissingExports = {
        "ImageList_GetIconSize",
        "ImageList_SetIconSize",
    };

    const wchar_t* sys32 = L"C:\\Windows\\System32";
    if (GetFileAttributesW(sys32) != INVALID_FILE_ATTRIBUTES) {
        CopyShimDll(shim64.c_str(), sys32);
        PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\user32.dll");
        PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\ole32.dll");
        PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\gdi32.dll");
        PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\comctl32.dll");
        PatchPeFileExports(L"C:\\Windows\\System32\\forwarders\\gdi32.dll", kGdi32MissingExports);
        PatchPeFileExports(L"C:\\Windows\\System32\\forwarders\\user32.dll", kUser32MissingExports);
        PatchPeFileExports(L"C:\\Windows\\System32\\forwarders\\comctl32.dll", kComctl32MissingExports);
        CopyFileW(L"C:\\Windows\\System32\\forwarders\\user32.dll", L"C:\\Windows\\System32\\user32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\System32\\forwarders\\ole32.dll", L"C:\\Windows\\System32\\ole32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\System32\\forwarders\\gdi32.dll", L"C:\\Windows\\System32\\gdi32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\System32\\forwarders\\comctl32.dll", L"C:\\Windows\\System32\\comctl32.dll", FALSE);
    }

    const wchar_t* sysWow64 = L"C:\\Windows\\SysWOW64";
    const wchar_t* sysWow64Fwd = L"C:\\Windows\\SysWOW64\\forwarders";
    if (GetFileAttributesW(sysWow64Fwd) != INVALID_FILE_ATTRIBUTES) {
        CopyShimDll(shim86.c_str(), sysWow64);
        PatchPeFileImports(L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll");
        PatchPeFileImports(L"C:\\Windows\\SysWOW64\\forwarders\\ole32.dll");
        PatchPeFileImports(L"C:\\Windows\\SysWOW64\\forwarders\\gdi32.dll");
        PatchPeFileImports(L"C:\\Windows\\SysWOW64\\forwarders\\comctl32.dll");
        PatchPeFileExports(L"C:\\Windows\\SysWOW64\\forwarders\\gdi32.dll", kGdi32MissingExports);
        PatchPeFileExports(L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll", kUser32MissingExports);
        PatchPeFileExports(L"C:\\Windows\\SysWOW64\\forwarders\\comctl32.dll", kComctl32MissingExports);
        CopyFileW(L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll", L"C:\\Windows\\SysWOW64\\user32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\SysWOW64\\forwarders\\ole32.dll", L"C:\\Windows\\SysWOW64\\ole32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\SysWOW64\\forwarders\\gdi32.dll", L"C:\\Windows\\SysWOW64\\gdi32.dll", FALSE);
        CopyFileW(L"C:\\Windows\\SysWOW64\\forwarders\\comctl32.dll", L"C:\\Windows\\SysWOW64\\comctl32.dll", FALSE);
    }

    return 0;
}
