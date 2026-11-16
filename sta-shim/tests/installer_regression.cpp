// Fault-injection tests. All file, registry and privilege operations reachable
// from the included installer are replaced with an in-memory fake. The real
// workstation's Windows directory, registry and token are never modified.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

enum class Fault { None, Copy, CreateStage, Write, ShortWrite, Flush, Delete, SetAttributes, MoveOriginal, Replace,
    Rollback, Verify, RegOpen, RegDelete, RegRead, RegWrite, Privilege,
    MarkerQuery, MarkerCreate, MarkerDisposition, MarkerRename, MarkerClose };
struct File {
    std::vector<BYTE> Bytes;
    DWORD Attributes = FILE_ATTRIBUTE_NORMAL;
    bool Written = false, ImageMapped = false, RefusePosixDelete = false;
};
struct OpenFile { std::wstring Path; size_t Position = 0; bool DeleteOnClose = false, PosixOnClose = false; };
std::map<std::wstring, File> files;
std::map<HANDLE, OpenFile> handles;
std::set<std::wstring> directories;
std::set<std::wstring> knownDlls;
struct RegistryValue {
    DWORD Type = 0;
    std::vector<BYTE> Bytes;
    bool operator==(const RegistryValue& other) const { return Type == other.Type && Bytes == other.Bytes; }
};
std::map<REGSAM, std::map<std::wstring, RegistryValue>> dispenserValues;
std::map<HKEY, REGSAM> registryHandles;
bool replaceRegistryOnFailure = false;
RegistryValue replacementRegistryValue;
Fault fault = Fault::None;
int failAt = 1, faultCalls = 0, copies = 0, registryDeletes = 0, mutations = 0, failures = 0;
bool faultHit = false, liveTruncated = false, unsafeFileOpen = false;
LONG lastFileStatus = 0;
bool hideNativeStatus = false;
unsigned statusLookups = 0, markerCreates = 0, namespaceReplacements = 0, stageCreates = 0;
std::vector<std::vector<BYTE>> unlinkedImageViews;
ULONG_PTR nextHandle = 0x10000;

bool EndsWith(const std::wstring& text, const wchar_t* suffix) {
    size_t length = wcslen(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

bool Fail(Fault operation) {
    if (fault != operation || ++faultCalls != failAt) return false;
    faultHit = true;
    lastFileStatus = static_cast<LONG>(0xC0000022u);
    SetLastError(ERROR_ACCESS_DENIED);
    return true;
}

DWORD WINAPI FakeAttributes(LPCWSTR path) {
    if (directories.count(path)) return FILE_ATTRIBUTE_DIRECTORY;
    auto found = files.find(path);
    if (found != files.end()) return found->second.Attributes;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_FILE_ATTRIBUTES;
}

HANDLE WINAPI FakeCreateFile(LPCWSTR path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD flags, HANDLE) {
    if (disposition == CREATE_NEW && EndsWith(path, L".unlink")) ++markerCreates;
    if (std::wstring(path).find(L"C:\\Windows\\") == 0 &&
        (access & (GENERIC_WRITE | DELETE | FILE_WRITE_ATTRIBUTES | FILE_ADD_FILE)) != 0 &&
        (flags & FILE_FLAG_BACKUP_SEMANTICS) == 0) {
        unsafeFileOpen = true;
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    auto found = files.find(path);
    if (disposition == OPEN_EXISTING && found == files.end() && !directories.count(path)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    if (disposition == CREATE_NEW && found != files.end()) {
        SetLastError(ERROR_FILE_EXISTS);
        return INVALID_HANDLE_VALUE;
    }
    if (disposition == CREATE_NEW) {
        if (EndsWith(path, L".new")) {
            ++stageCreates;
            if (Fail(Fault::CreateStage)) return INVALID_HANDLE_VALUE;
        } else if (EndsWith(path, L".unlink")) {
            if (Fail(Fault::MarkerCreate)) return INVALID_HANDLE_VALUE;
        } else {
            ++copies;
            if (Fail(Fault::Copy)) return INVALID_HANDLE_VALUE;
        }
    }
    if (disposition == CREATE_ALWAYS && !EndsWith(path, L".new")) liveTruncated = true;
    if (disposition == CREATE_ALWAYS || disposition == CREATE_NEW) { files[path] = {}; ++mutations; }
    HANDLE handle = reinterpret_cast<HANDLE>(++nextHandle);
    handles[handle] = {path, 0, (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0};
    return handle;
}

BOOL WINAPI FakeReadFile(HANDLE handle, LPVOID output, DWORD length, LPDWORD read, LPOVERLAPPED) {
    auto found = handles.find(handle);
    if (found == handles.end()) return FALSE;
    auto& opened = found->second;
    const auto& bytes = files[opened.Path].Bytes;
    size_t available = bytes.size() - opened.Position;
    *read = static_cast<DWORD>((std::min)(available, static_cast<size_t>(length)));
    memcpy(output, bytes.data() + opened.Position, *read);
    opened.Position += *read;
    if (files[opened.Path].Written && *read && Fail(Fault::Verify)) static_cast<BYTE*>(output)[0] ^= 1;
    return TRUE;
}

BOOL WINAPI FakeWriteFile(HANDLE handle, LPCVOID input, DWORD length, LPDWORD written, LPOVERLAPPED) {
    if (Fail(Fault::Write)) return FALSE;
    auto& opened = handles.at(handle);
    *written = Fail(Fault::ShortWrite) ? length / 2 : length;
    auto& bytes = files[opened.Path].Bytes;
    files[opened.Path].Written = true;
    bytes.resize(opened.Position + *written);
    memcpy(bytes.data() + opened.Position, input, *written);
    opened.Position += *written;
    ++mutations;
    return TRUE;
}

BOOL WINAPI FakeSize(HANDLE handle, PLARGE_INTEGER size) {
    size->QuadPart = files.at(handles.at(handle).Path).Bytes.size();
    return TRUE;
}

BOOL WINAPI FakeFlush(HANDLE) { return Fail(Fault::Flush) ? FALSE : TRUE; }
BOOL WINAPI FakeClose(HANDLE handle) {
    auto found = handles.find(handle);
    if (found != handles.end() && found->second.DeleteOnClose) {
        if (Fail(Fault::MarkerClose)) return FALSE;
        files.erase(found->second.Path);
        ++mutations;
    }
    handles.erase(handle);
    lastFileStatus = 0; // Native status must be captured before closing the file.
    return TRUE;
}

LONG WINAPI FakeLastFileStatus() { return lastFileStatus; }
FARPROC WINAPI FakeGetProcAddress(HMODULE, LPCSTR name) {
    if (std::strcmp(name, "RtlGetLastNtStatus")) return nullptr;
    ++statusLookups;
    return hideNativeStatus ? nullptr : reinterpret_cast<FARPROC>(FakeLastFileStatus);
}

BOOL WINAPI FakeGetFileInformation(HANDLE handle, FILE_INFO_BY_HANDLE_CLASS type, LPVOID output, DWORD size) {
    auto found = handles.find(handle);
    if (found == handles.end() || type != FileBasicInfo || size < sizeof(FILE_BASIC_INFO)) {
        SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (Fail(Fault::MarkerQuery)) return FALSE;
    *static_cast<FILE_BASIC_INFO*>(output) = {};
    static_cast<FILE_BASIC_INFO*>(output)->FileAttributes = files.at(found->second.Path).Attributes;
    return TRUE;
}

BOOL WINAPI FakeSetAttributes(LPCWSTR path, DWORD attributes) {
    if (Fail(Fault::SetAttributes)) return FALSE;
    auto found = files.find(path);
    if (found == files.end()) return FALSE;
    found->second.Attributes = attributes;
    ++mutations;
    return TRUE;
}

BOOL WINAPI FakeDelete(LPCWSTR path) {
    if (Fail(Fault::Delete)) return FALSE;
    auto found = files.find(path);
    if (found != files.end() && found->second.ImageMapped)
        unlinkedImageViews.push_back(found->second.Bytes);
    if (!files.erase(path)) return FALSE;
    ++mutations;
    return TRUE;
}

BOOL WINAPI FakeMove(LPCWSTR source, LPCWSTR destination, DWORD) {
    std::wstring from = source, to = destination;
    if (EndsWith(to, L".orig") && Fail(Fault::MoveOriginal)) return FALSE;
    if (EndsWith(from, L".new") && (Fail(Fault::Replace) || fault == Fault::Rollback)) {
        faultHit = true;
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (EndsWith(from, L".orig") && fault == Fault::Rollback) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    auto found = files.find(from);
    if (found == files.end() || files.count(to)) return FALSE;
    files.emplace(to, std::move(found->second));
    files.erase(found);
    ++mutations;
    return TRUE;
}

BOOL WINAPI FakeCopy(LPCWSTR source, LPCWSTR destination, BOOL noOverwrite) {
    ++copies;
    if (Fail(Fault::Copy)) return FALSE;
    auto found = files.find(source);
    if (found == files.end() || (noOverwrite && files.count(destination))) return FALSE;
    files[destination] = found->second;
    ++mutations;
    return TRUE;
}

BOOL WINAPI FakeSetFileInformation(HANDLE handle, FILE_INFO_BY_HANDLE_CLASS type, LPVOID input, DWORD) {
    lastFileStatus = 0;
    auto found = handles.find(handle);
    if (found == handles.end()) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (type == FileBasicInfo) {
        return FakeSetAttributes(found->second.Path.c_str(), static_cast<FILE_BASIC_INFO*>(input)->FileAttributes);
    }
    if (type == FileDispositionInfo) {
        if (files.at(found->second.Path).ImageMapped) {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        return static_cast<FILE_DISPOSITION_INFO*>(input)->DeleteFile &&
            FakeDelete(found->second.Path.c_str());
    }
    if (type == FileDispositionInfoEx) {
        auto* disposition = static_cast<FILE_DISPOSITION_INFO_EX*>(input);
        if (disposition->Flags == (FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                FILE_DISPOSITION_FLAG_ON_CLOSE) && found->second.DeleteOnClose) {
            if (Fail(Fault::MarkerDisposition)) return FALSE;
            found->second.PosixOnClose = true;
            return TRUE;
        }
        if (disposition->Flags != (FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        const auto& file = files.at(found->second.Path);
        if ((file.ImageMapped && file.RefusePosixDelete) || (file.Attributes & FILE_ATTRIBUTE_READONLY)) {
            lastFileStatus = static_cast<LONG>(0xC0000121u);
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        return FakeDelete(found->second.Path.c_str());
    }
    if (type == FileRenameInfoEx) {
        auto* rename = static_cast<FILE_RENAME_INFO*>(input);
        std::wstring destination(rename->FileName, rename->FileNameLength / sizeof(wchar_t));
        if (rename->Flags != (FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS) ||
            rename->RootDirectory || !found->second.DeleteOnClose || !found->second.PosixOnClose ||
            destination.size() < 3 || destination[1] != L':' || destination[2] != L'\\') {
            SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
        }
        if (Fail(Fault::MarkerRename)) return FALSE;
        auto previous = files.find(destination);
        if (previous == files.end()) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
        if (previous->second.ImageMapped) unlinkedImageViews.push_back(previous->second.Bytes);
        std::wstring source = found->second.Path;
        File marker = std::move(files.at(source));
        files.erase(source);
        files[destination] = std::move(marker);
        found->second.Path = destination;
        ++namespaceReplacements; ++mutations;
        return TRUE;
    }
    if (type == FileRenameInfo) {
        auto* rename = static_cast<FILE_RENAME_INFO*>(input);
        std::wstring destination(rename->FileName, rename->FileNameLength / sizeof(wchar_t));
        if (rename->RootDirectory || rename->ReplaceIfExists || destination.size() < 3 ||
            destination[1] != L':' || destination[2] != L'\\') {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (!FakeMove(found->second.Path.c_str(), destination.c_str(), 0)) return FALSE;
        found->second.Path = destination;
        return TRUE;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI FakeOpenToken(HANDLE, DWORD, PHANDLE token) {
    if (Fail(Fault::Privilege)) return FALSE;
    *token = reinterpret_cast<HANDLE>(1);
    return TRUE;
}
BOOL WINAPI FakeLookupPrivilege(LPCWSTR, LPCWSTR, PLUID value) { *value = {}; return TRUE; }
BOOL WINAPI FakeAdjustToken(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD) {
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}
LSTATUS WINAPI FakeRegOpen(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY key) {
    if (Fail(Fault::RegOpen)) return ERROR_ACCESS_DENIED;
    *key = reinterpret_cast<HKEY>(2);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI FakeRegCreate(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD options, REGSAM access, const LPSECURITY_ATTRIBUTES, PHKEY key, LPDWORD) {
    if (options == REG_OPTION_NON_VOLATILE) {
        *key = reinterpret_cast<HKEY>(++nextHandle);
        registryHandles[*key] = access & (KEY_WOW64_32KEY | KEY_WOW64_64KEY);
        ++mutations;
        return ERROR_SUCCESS;
    }
    if (options != REG_OPTION_BACKUP_RESTORE) return ERROR_ACCESS_DENIED;
    *key = reinterpret_cast<HKEY>(2);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI FakeRegDelete(HKEY key, LPCWSTR name) {
    auto registration = registryHandles.find(key);
    if (registration != registryHandles.end()) {
        if (!dispenserValues[registration->second].erase(name ? name : L"")) return ERROR_FILE_NOT_FOUND;
        ++mutations;
        return ERROR_SUCCESS;
    }
    if (key != reinterpret_cast<HKEY>(2)) return ERROR_INVALID_HANDLE;
    ++registryDeletes;
    if (Fail(Fault::RegDelete)) return ERROR_ACCESS_DENIED;
    if (!knownDlls.erase(name)) return ERROR_FILE_NOT_FOUND;
    ++mutations;
    return ERROR_SUCCESS;
}
LSTATUS WINAPI FakeRegClose(HKEY key) { registryHandles.erase(key); return ERROR_SUCCESS; }
LSTATUS WINAPI FakeRegQuery(HKEY key, LPCWSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD size) {
    auto registration = registryHandles.find(key);
    if (registration == registryHandles.end() || !size) return ERROR_INVALID_HANDLE;
    if (Fail(Fault::RegRead)) return ERROR_ACCESS_DENIED;
    auto& values = dispenserValues[registration->second];
    auto found = values.find(name ? name : L"");
    if (found == values.end()) return ERROR_FILE_NOT_FOUND;
    if (type) *type = found->second.Type;
    DWORD capacity = *size;
    *size = static_cast<DWORD>(found->second.Bytes.size());
    if (!data) return ERROR_SUCCESS;
    if (capacity < *size) return ERROR_MORE_DATA;
    if (*size) memcpy(data, found->second.Bytes.data(), *size);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI FakeRegSet(HKEY key, LPCWSTR name, DWORD, DWORD type, const BYTE* data, DWORD size) {
    auto registration = registryHandles.find(key);
    if (registration == registryHandles.end() || (size && !data)) return ERROR_INVALID_PARAMETER;
    if (Fail(Fault::RegWrite)) {
        if (replaceRegistryOnFailure) dispenserValues[registration->second][L""] = replacementRegistryValue;
        return ERROR_ACCESS_DENIED;
    }
    RegistryValue value;
    value.Type = type;
    if (size) value.Bytes.assign(data, data + size);
    dispenserValues[registration->second][name ? name : L""] = std::move(value);
    ++mutations;
    return ERROR_SUCCESS;
}
DWORD WINAPI FakeModuleName(HMODULE, LPWSTR output, DWORD capacity) {
    const wchar_t* name = L"C:\\payload\\install_sta_shim.exe";
    if (capacity <= wcslen(name)) return capacity;
    wcscpy_s(output, capacity, name);
    return static_cast<DWORD>(wcslen(name));
}

} // namespace

#define GetFileAttributesW FakeAttributes
#define CreateFileW FakeCreateFile
#define ReadFile FakeReadFile
#define WriteFile FakeWriteFile
#define GetFileSizeEx FakeSize
#define FlushFileBuffers FakeFlush
#define CloseHandle FakeClose
#define SetFileAttributesW FakeSetAttributes
#define DeleteFileW FakeDelete
#define MoveFileExW FakeMove
#define CopyFileW FakeCopy
#define SetFileInformationByHandle FakeSetFileInformation
#define GetFileInformationByHandleEx FakeGetFileInformation
#define GetProcAddress FakeGetProcAddress
#define OpenProcessToken FakeOpenToken
#define LookupPrivilegeValueW FakeLookupPrivilege
#define AdjustTokenPrivileges FakeAdjustToken
#define RegOpenKeyExW FakeRegOpen
#define RegCreateKeyExW FakeRegCreate
#define RegDeleteValueW FakeRegDelete
#define RegCloseKey FakeRegClose
#define RegSetValueExW FakeRegSet
#define RegQueryValueExW FakeRegQuery
#define GetModuleFileNameW FakeModuleName
#define wmain InstallerUnderTest
#include "../install_sta_shim.cpp"
#undef wmain

namespace {

std::vector<BYTE> Fixture(WORD machine) {
    std::vector<BYTE> bytes(0xA00, 0);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(bytes.data() + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = machine;
    nt->FileHeader.NumberOfSections = 2;
    IMAGE_DATA_DIRECTORY* directories = nullptr;
    if (machine == IMAGE_FILE_MACHINE_I386) {
        auto* pe = reinterpret_cast<IMAGE_NT_HEADERS32*>(nt);
        pe->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
        pe->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        pe->OptionalHeader.SectionAlignment = 0x1000;
        pe->OptionalHeader.FileAlignment = 0x200;
        pe->OptionalHeader.SizeOfHeaders = 0x400;
        pe->OptionalHeader.SizeOfImage = 0x3000;
        pe->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        directories = pe->OptionalHeader.DataDirectory;
    } else {
        auto* pe = reinterpret_cast<IMAGE_NT_HEADERS64*>(nt);
        pe->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        pe->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        pe->OptionalHeader.SectionAlignment = 0x1000;
        pe->OptionalHeader.FileAlignment = 0x200;
        pe->OptionalHeader.SizeOfHeaders = 0x400;
        pe->OptionalHeader.SizeOfImage = 0x3000;
        pe->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        directories = pe->OptionalHeader.DataDirectory;
    }
    directories[IMAGE_DIRECTORY_ENTRY_IMPORT] = {0x1000, 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR)};
    directories[IMAGE_DIRECTORY_ENTRY_EXPORT] = {0x2000, 0x100};
    auto* sections = IMAGE_FIRST_SECTION(nt);
    sections[0].VirtualAddress = 0x1000;
    sections[0].Misc.VirtualSize = sections[0].SizeOfRawData = 0x200;
    sections[0].PointerToRawData = 0x400;
    sections[1].VirtualAddress = 0x2000;
    sections[1].Misc.VirtualSize = sections[1].SizeOfRawData = 0x400;
    sections[1].PointerToRawData = 0x600;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes.data() + 0x400);
    imports->Name = 0x1080;
    imports->FirstThunk = 0x10C0;
    memcpy(bytes.data() + 0x480, "api-ms-win-core-apiquery-l1-1-0.dll", 36);
    auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(bytes.data() + 0x600);
    exports->Name = 0x2060;
    exports->Base = 1;
    exports->NumberOfFunctions = exports->NumberOfNames = 1;
    exports->AddressOfFunctions = 0x2040;
    exports->AddressOfNames = 0x2044;
    exports->AddressOfNameOrdinals = 0x2048;
    *reinterpret_cast<DWORD*>(bytes.data() + 0x640) = 0x1100;
    *reinterpret_cast<DWORD*>(bytes.data() + 0x644) = 0x2080;
    memcpy(bytes.data() + 0x660, "fixture.dll", 12);
    memcpy(bytes.data() + 0x680, "Existing", 9);
    return bytes;
}

IMAGE_DATA_DIRECTORY* FixtureDirectories(std::vector<BYTE>& bytes) {
    auto* nt = GetPeHeaders(bytes);
    return nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DataDirectory :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DataDirectory;
}

WORD FixtureDllCharacteristics(std::vector<BYTE>& bytes) {
    auto* nt = GetPeHeaders(bytes);
    return nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DllCharacteristics :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DllCharacteristics;
}

std::vector<BYTE> HotpatchFixture(WORD machine) {
    auto bytes = Fixture(machine);
    auto* nt = GetPeHeaders(bytes);
    WORD flags = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT |
        IMAGE_DLLCHARACTERISTICS_GUARD_CF | IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
    if (machine == IMAGE_FILE_MACHINE_I386)
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DllCharacteristics = flags;
    else
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DllCharacteristics = flags;
    auto* directories = FixtureDirectories(bytes);
    directories[IMAGE_DIRECTORY_ENTRY_SECURITY] = {0x980, 0x80};
    directories[IMAGE_DIRECTORY_ENTRY_DEBUG] = {0x2100, 2 * sizeof(IMAGE_DEBUG_DIRECTORY)};
    auto* debug = reinterpret_cast<IMAGE_DEBUG_DIRECTORY*>(bytes.data() + 0x700);
    debug[0].Type = IMAGE_DEBUG_TYPE_REPRO;
    debug[0].SizeOfData = sizeof(DWORD);
    debug[0].AddressOfRawData = 0x2140;
    debug[0].PointerToRawData = 0x740;
    debug[1].Type = IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS;
    debug[1].SizeOfData = 2 * sizeof(DWORD);
    debug[1].AddressOfRawData = 0x2148;
    debug[1].PointerToRawData = 0x748;
    *reinterpret_cast<DWORD*>(bytes.data() + 0x740) = 0xDEADBEEF;
    *reinterpret_cast<DWORD*>(bytes.data() + 0x748) =
        IMAGE_DLLCHARACTERISTICS_EX_HOTPATCH_COMPATIBLE | IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT | 0x100;
    *reinterpret_cast<DWORD*>(bytes.data() + 0x74C) = 0x12345678;
    return bytes;
}

std::vector<BYTE> CompanionFixture(WORD machine, const wchar_t* name) {
    auto bytes = Fixture(machine);
    bytes.resize(0x1200);
    auto* nt = GetPeHeaders(bytes);
    auto* section = IMAGE_FIRST_SECTION(nt) + 1;
    section->Misc.VirtualSize = section->SizeOfRawData = 0xC00;
    auto* data = machine == IMAGE_FILE_MACHINE_I386 ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DataDirectory :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DataDirectory;
    data[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = 0xC00;
    auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(bytes.data() + 0x600);
    const auto& contract = CompanionExports(name);
    exports->Base = contract[0].Ordinal ? contract[0].Ordinal : 1;
    exports->NumberOfNames = exports->NumberOfFunctions = static_cast<DWORD>(contract.size());
    exports->AddressOfFunctions = 0x2040;
    exports->AddressOfNames = 0x2080;
    exports->AddressOfNameOrdinals = 0x20C0;
    exports->Name = 0x20F0;
    memcpy(bytes.data() + 0x6F0, "fixture.dll", 12);
    size_t cursor = 0x700;
    auto append = [&](const char* value) {
        DWORD rva = static_cast<DWORD>(cursor - 0x600 + 0x2000);
        size_t count = strlen(value) + 1;
        memcpy(bytes.data() + cursor, value, count); cursor += count;
        return rva;
    };
    for (size_t i = 0; i < contract.size(); ++i) {
        *reinterpret_cast<DWORD*>(bytes.data() + 0x680 + i * 4) = append(contract[i].Name);
        *reinterpret_cast<WORD*>(bytes.data() + 0x6C0 + i * 2) = static_cast<WORD>(i);
        *reinterpret_cast<DWORD*>(bytes.data() + 0x640 + i * 4) = append(contract[i].Target);
    }
    return bytes;
}

void Reset(bool wow64 = true) {
    files.clear(); handles.clear(); directories.clear();
    dispenserValues.clear(); registryHandles.clear(); replaceRegistryOnFailure = false;
    knownDlls = {L"user32", L"ole32", L"gdi32", L"SHELL32", L"gdiplus", L"shlwapi"};
    fault = Fault::None; failAt = 1; faultCalls = copies = registryDeletes = mutations = 0;
    faultHit = liveTruncated = unsafeFileOpen = false;
    lastFileStatus = 0; hideNativeStatus = false;
    statusLookups = markerCreates = namespaceReplacements = stageCreates = 0;
    unlinkedImageViews.clear();
    directories.insert(L"C:\\payload");
    files[L"C:\\payload\\nanoserver_sta_shim_x64.dll"] = {Fixture(IMAGE_FILE_MACHINE_AMD64)};
    for (const wchar_t* name : {L"olepro32", L"comsvcs", L"mtxdm"}) {
        files[std::wstring(L"C:\\payload\\") + name + L"_x64.dll"] = {CompanionFixture(IMAGE_FILE_MACHINE_AMD64, name)};
        files[std::wstring(L"C:\\payload\\") + name + L"_x86.dll"] = {CompanionFixture(IMAGE_FILE_MACHINE_I386, name)};
    }
    for (const std::wstring& system : {std::wstring(L"C:\\Windows\\System32"), std::wstring(L"C:\\Windows\\SysWOW64")}) {
        bool isWow = system.find(L"SysWOW64") != std::wstring::npos;
        if (isWow && !wow64) continue;
        WORD machine = isWow ? IMAGE_FILE_MACHINE_I386 : IMAGE_FILE_MACHINE_AMD64;
        directories.insert(system);
        directories.insert(system + L"\\forwarders");
        files[system + L"\\forwarders\\kernel32.dll"] = {Fixture(machine)};
        for (const wchar_t* name : {L"user32.dll", L"ole32.dll", L"gdi32.dll", L"comctl32.dll", L"shell32.dll", L"winmm.dll", L"shlwapi.dll"}) {
            files[system + L"\\forwarders\\" + name] = {Fixture(machine)};
            files[system + L"\\" + name] = {Fixture(machine)};
        }
    }
    if (wow64) files[L"C:\\payload\\nanoserver_sta_shim_x86.dll"] = {Fixture(IMAGE_FILE_MACHINE_I386)};
}

int Install(bool requireWow = true, bool alias = false) {
    wchar_t exe[] = L"install_sta_shim.exe";
    wchar_t payload[] = L"C:\\payload";
    wchar_t flag[] = L"--require-wow64";
    wchar_t alternate[] = L"/require-x86";
    wchar_t* args[] = {exe, payload, alias ? alternate : flag};
    return InstallerUnderTest(requireWow ? 3 : 2, args);
}

void Check(const char* name, bool success) {
    std::printf("CHECK %s %s\n", name, success ? "PASS" : "FAIL");
    if (!success) ++failures;
}

RegistryValue RegistryText(DWORD type, const wchar_t* text) {
    const BYTE* start = reinterpret_cast<const BYTE*>(text);
    return {type, std::vector<BYTE>(start, start + (wcslen(text) + 1) * sizeof(wchar_t))};
}

void SeedDispenserValues() {
    for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        dispenserValues[view][L""] = RegistryText(REG_EXPAND_SZ, L"%SystemRoot%\\prior.dll");
        dispenserValues[view][L"ThreadingModel"] = RegistryText(REG_SZ, L"Apartment");
    }
    dispenserValues[KEY_WOW64_32KEY][L"ThreadingModel"] = {REG_BINARY, {1, 2, 3}};
}

DWORD NamedExportRva(std::vector<BYTE>& bytes, const char* wanted) {
    auto* nt = GetPeHeaders(bytes);
    const auto* data = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DataDirectory :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DataDirectory;
    DWORD offset = RvaToOffset(nt, nt->FileHeader.NumberOfSections, data[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes.data() + offset);
    DWORD names = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfNames);
    DWORD ordinals = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfNameOrdinals);
    DWORD functions = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfFunctions);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        std::string name;
        ReadPeString(bytes, RvaToOffset(nt, nt->FileHeader.NumberOfSections,
            reinterpret_cast<const DWORD*>(bytes.data() + names)[i]), name);
        if (name == wanted)
            return reinterpret_cast<const DWORD*>(bytes.data() + functions)[
                reinterpret_cast<const WORD*>(bytes.data() + ordinals)[i]];
    }
    return 0;
}

void TestCombinedPeAtomicity() {
    const wchar_t* target = L"C:\\Windows\\System32\\forwarders\\ole32.dll";
    const std::wstring backup = std::wstring(target) + L".orig";
    const std::wstring staged = std::wstring(target) + L".new";
    for (WORD machine : {IMAGE_FILE_MACHINE_I386, IMAGE_FILE_MACHINE_AMD64}) {
        Reset();
        files[target] = {HotpatchFixture(machine)};
        Check("combined_import_export_has_one_verified_commit",
            PatchPeFileImportsAndExports(target, {"_hread"}) && stageCreates == 1 &&
            VerifyPatchedPe(target, {"_hread"}) && !files.count(backup) && !files.count(staged) &&
            handles.empty() && !liveTruncated);
        auto current = files[target].Bytes;
        int priorMutations = mutations;
        unsigned priorStages = stageCreates;
        Check("combined_import_export_is_idempotent_without_replacement",
            PatchPeFileImportsAndExports(target, {"_hread"}) && files[target].Bytes == current &&
            mutations == priorMutations && stageCreates == priorStages && handles.empty());

        Reset();
        files[target] = {HotpatchFixture(machine)};
        reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(files[target].Bytes.data() + 0x600)->NumberOfFunctions = 0xFFFFFFFF;
        auto original = files[target].Bytes;
        Check("invalid_export_leaves_imports_and_live_bytes_untouched",
            !PatchPeFileImportsAndExports(target, {"_hread"}) && files[target].Bytes == original &&
            mutations == 0 && stageCreates == 0 && handles.empty());

        Reset();
        files[target] = {HotpatchFixture(machine)};
        Check("combined_code_replacement_preserves_unrelated_named_export",
            PatchPeFileImportsAndExports(target, {"_hread"}, {}, true) &&
            VerifyPatchedPe(target, {"_hread"}) && NamedExportRva(files[target].Bytes, "Existing") == 0x1100 &&
            stageCreates == 1 && !liveTruncated);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        Check("combined_ordinal_rewrite_preserves_named_alias",
            PatchPeFileImportsAndExports(target, {}, {{1, "kernel32.GetTickCount"}}) &&
            VerifyPatchedPe(target, {}) && VerifyOrdinalForwarders(target, {{1, "kernel32.GetTickCount"}}) &&
            NamedExportRva(files[target].Bytes, "Existing") == 0x1100 && stageCreates == 1);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        original = files[target].Bytes;
        fault = Fault::Verify;
        failAt = 2;
        Check("combined_live_verification_failure_restores_exact_original",
            !PatchPeFileImportsAndExports(target, {"_hread"}) && faultHit &&
            files[target].Bytes == original && !files.count(backup) && !files.count(staged) &&
            handles.empty() && !liveTruncated);
    }
}

void TestHotpatchMetadata() {
    const wchar_t* target = L"C:\\Windows\\System32\\forwarders\\user32.dll";
    const WORD mitigations = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT |
        IMAGE_DLLCHARACTERISTICS_GUARD_CF;
    for (WORD machine : {IMAGE_FILE_MACHINE_I386, IMAGE_FILE_MACHINE_AMD64}) {
        Reset();
        files[target] = {HotpatchFixture(machine)};
        files[target].ImageMapped = true;
        Check("R18_mapped_import_backup_is_unlinked_without_truncation",
            PatchPeFileImports(target) && !files.count(std::wstring(target) + L".orig") && !liveTruncated);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        files[target].ImageMapped = true;
        Check("R18_mapped_export_backup_is_unlinked_without_truncation",
            PatchPeFileExports(target, {"_hread"}) &&
            !files.count(std::wstring(target) + L".orig") && !liveTruncated);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        auto original = files[target].Bytes;
        fault = Fault::Delete;
        Check("R18_backup_cleanup_failure_remains_fatal_and_preserves_original",
            !PatchPeFileImports(target) && faultHit &&
            files.count(std::wstring(target) + L".orig") &&
            files[std::wstring(target) + L".orig"].Bytes == original && !liveTruncated);

        const std::wstring backup = std::wstring(target) + L".orig";
        const std::wstring marker = backup + L".unlink";
        for (bool exports : {false, true}) {
            Reset();
            files[target] = {HotpatchFixture(machine)};
            files[target].ImageMapped = files[target].RefusePosixDelete = true;
            auto saved = files[target].Bytes;
            bool patched = exports ? PatchPeFileExports(target, {"_hread"}) : PatchPeFileImports(target);
            Check(exports ? "R18_mapped_export_refusal_uses_posix_replacement" :
                "R18_mapped_import_refusal_uses_posix_replacement",
                patched && namespaceReplacements == 1 && markerCreates == 1 &&
                !files.count(backup) && !files.count(marker) && handles.empty() &&
                unlinkedImageViews.size() == 1 && unlinkedImageViews[0] == saved && !liveTruncated);
        }
        for (Fault operation : {Fault::MarkerQuery, Fault::MarkerCreate, Fault::MarkerDisposition, Fault::MarkerRename}) {
            Reset();
            files[target] = {HotpatchFixture(machine)};
            files[target].ImageMapped = files[target].RefusePosixDelete = true;
            auto saved = files[target].Bytes;
            fault = operation;
            Check("R18_mapped_cleanup_failure_preserves_backup_and_cleans_owned_marker",
                !PatchPeFileImports(target) && faultHit && files.count(backup) &&
                files[backup].Bytes == saved && !files.count(marker) && handles.empty() &&
                !namespaceReplacements && !liveTruncated);
        }
        Reset();
        files[target] = {HotpatchFixture(machine)};
        files[target].ImageMapped = files[target].RefusePosixDelete = true;
        fault = Fault::MarkerClose;
        Check("R18_mapped_cleanup_close_failure_remains_fatal",
            !PatchPeFileImports(target) && faultHit && namespaceReplacements == 1 && !liveTruncated);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        files[target].ImageMapped = files[target].RefusePosixDelete = true;
        original = files[target].Bytes;
        files[marker] = {{0x31, 0x41, 0x59}};
        Check("R18_mapped_cleanup_never_overwrites_existing_marker",
            !PatchPeFileImports(target) && markerCreates == 1 && files.count(backup) &&
            files[backup].Bytes == original && files[marker].Bytes == std::vector<BYTE>({0x31, 0x41, 0x59}) &&
            !namespaceReplacements && !liveTruncated);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        files[target].ImageMapped = files[target].RefusePosixDelete = true;
        original = files[target].Bytes;
        hideNativeStatus = true;
        Check("R18_missing_native_failure_status_fails_closed",
            !PatchPeFileImports(target) && statusLookups && files.count(backup) &&
            files[backup].Bytes == original && !markerCreates && !liveTruncated);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        files[target].ImageMapped = files[target].RefusePosixDelete = true;
        files[target].Attributes |= FILE_ATTRIBUTE_READONLY;
        original = files[target].Bytes;
        Check("R18_mapped_cleanup_does_not_ignore_readonly_attribute",
            !DeleteFileForRestore(target) && files[target].Bytes == original &&
            (files[target].Attributes & FILE_ATTRIBUTE_READONLY) && !markerCreates && !liveTruncated);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        Check("modified_imports_remove_only_hotpatch_compatibility",
            PatchPeFileImports(target) && *reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x748) ==
                (IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT | 0x100));
        Check("modified_imports_preserve_security_mitigations",
            FixtureDllCharacteristics(files[target].Bytes) == mitigations);
        Check("modified_imports_preserve_other_debug_data",
            *reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x740) == 0xDEADBEEF &&
            *reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x74C) == 0x12345678);
        Check("modified_imports_clear_invalidated_signature",
            FixtureDirectories(files[target].Bytes)[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress == 0 &&
            FixtureDirectories(files[target].Bytes)[IMAGE_DIRECTORY_ENTRY_SECURITY].Size == 0);

        Reset();
        files[target] = {HotpatchFixture(machine)};
        Check("modified_exports_remove_only_hotpatch_compatibility",
            PatchPeFileExports(target, {"_hread"}) &&
            *reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x748) ==
                (IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT | 0x100) &&
            FixtureDllCharacteristics(files[target].Bytes) == mitigations);
        auto patched = files[target].Bytes;
        *reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x748) |=
            IMAGE_DLLCHARACTERISTICS_EX_HOTPATCH_COMPATIBLE;
        Check("verification_rejects_stale_hotpatch_compatibility",
            !VerifyPatchedPe(target, {"_hread"}, false));
        Check("existing_exports_repair_stale_hotpatch_metadata",
            PatchPeFileExports(target, {"_hread"}) &&
            !(*reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x748) &
                IMAGE_DLLCHARACTERISTICS_EX_HOTPATCH_COMPATIBLE) && patched == files[target].Bytes);
        Check("hotpatch_metadata_repair_is_idempotent",
            PatchPeFileExports(target, {"_hread"}) &&
            !(*reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x748) &
                IMAGE_DLLCHARACTERISTICS_EX_HOTPATCH_COMPATIBLE) && patched == files[target].Bytes);

        Reset();
        auto nonmapped = HotpatchFixture(machine);
        reinterpret_cast<IMAGE_DEBUG_DIRECTORY*>(nonmapped.data() + 0x700)[1].AddressOfRawData = 0;
        files[target] = {nonmapped};
        Check("nonmapped_extended_characteristics_are_bounded_and_repaired",
            PatchPeFileImports(target) &&
            *reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x748) ==
                (IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT | 0x100));

        for (int invalid = 0; invalid < 8; ++invalid) {
            Reset();
            auto bytes = HotpatchFixture(machine);
            auto& directory = FixtureDirectories(bytes)[IMAGE_DIRECTORY_ENTRY_DEBUG];
            auto& extended = reinterpret_cast<IMAGE_DEBUG_DIRECTORY*>(bytes.data() + 0x700)[1];
            switch (invalid) {
            case 0: directory.VirtualAddress = MAXDWORD; break;
            case 1: directory.Size = MAXDWORD; break;
            case 2: --directory.Size; break;
            case 3: extended.PointerToRawData = MAXDWORD; break;
            case 4: extended.SizeOfData = sizeof(DWORD) - 1; break;
            case 5: extended.AddressOfRawData = 0x2150; break;
            case 6: extended.PointerToRawData = 0x9FF; extended.AddressOfRawData = 0; break;
            case 7: directory.VirtualAddress = 0; break;
            }
            files[target] = {bytes};
            Check("invalid_hotpatch_metadata_rejected_without_file_mutation",
                !PatchPeFileImports(target) && files[target].Bytes == bytes && mutations == 0);
        }
    }
    Reset();
    const wchar_t* wow = L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll";
    files[wow] = {HotpatchFixture(IMAGE_FILE_MACHINE_I386)};
    FixtureDirectories(files[wow].Bytes)[IMAGE_DIRECTORY_ENTRY_DEBUG].Size = MAXDWORD;
    Check("hotpatch_metadata_preflight_covers_both_architectures",
        Install() != 0 && mutations == 0 && registryDeletes == 0);
}

} // namespace

int main() {
    TestHotpatchMetadata();
    TestCombinedPeAtomicity();
    Reset(false);
    Check("R02_x64_without_x86_payload", Install(false) == 0 && registryDeletes == 6);
    Reset(false);
    Check("R02_required_wow64_preflight", Install() != 0 && mutations == 0);
    Reset(false);
    Check("R02_require_x86_alias", Install(true, true) != 0 && mutations == 0);
    Reset();
    files.erase(L"C:\\Windows\\SysWOW64\\forwarders\\gdi32.dll");
    Check("R02_destination_preflight", Install() != 0 && mutations == 0);
    Reset();
    files.erase(L"C:\\payload\\nanoserver_sta_shim_x64.dll");
    Check("R02_payload_preflight", Install() != 0 && mutations == 0);
    Reset();
    files.erase(L"C:\\payload\\olepro32_x86.dll");
    Check("companion_payload_preflight_before_mutation", Install() != 0 && mutations == 0);
    Reset();
    files[L"C:\\payload\\olepro32_x86.dll"] = {Fixture(IMAGE_FILE_MACHINE_I386)};
    Check("companion_missing_exports_preflight_before_mutation", Install() != 0 && mutations == 0);
    Reset();
    *reinterpret_cast<DWORD*>(files[L"C:\\payload\\olepro32_x86.dll"].Bytes.data() + 0x600 + 16) = 247;
    Check("companion_wrong_ordinals_preflight_before_mutation", Install() != 0 && mutations == 0);
    Reset();
    *reinterpret_cast<DWORD*>(files[L"C:\\payload\\comsvcs_x64.dll"].Bytes.data() + 0x640) = 0x1100;
    Check("companion_nonforwarder_preflight_before_mutation", Install() != 0 && mutations == 0);
    for (Fault operation : {Fault::Privilege, Fault::RegOpen}) {
        Reset(); fault = operation;
        Check("R02_preinstall_failure", Install() != 0 && faultHit && mutations == 0);
    }
    for (int index = 1; index <= 4; ++index) {
        Reset(); fault = Fault::Copy; failAt = index;
        Check("R02_required_copy_failure", Install() != 0 && faultHit && copies == index && registryDeletes == 0);
    }
    for (Fault operation : {Fault::Write, Fault::ShortWrite, Fault::Flush, Fault::Verify}) {
        Reset(); fault = operation;
        Check("R02_payload_write_failure", Install() != 0 && faultHit && registryDeletes == 0 && !liveTruncated);
    }
    for (Fault operation : {Fault::CreateStage, Fault::Write, Fault::ShortWrite, Fault::Flush,
                            Fault::SetAttributes, Fault::MoveOriginal, Fault::Replace, Fault::Verify}) {
        Reset(); fault = operation;
        auto original = files[L"C:\\Windows\\System32\\forwarders\\user32.dll"].Bytes;
        Check("R02_R18_patch_failure_preserves_original",
            !PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\user32.dll") && faultHit && !liveTruncated &&
            files[L"C:\\Windows\\System32\\forwarders\\user32.dll"].Bytes == original && registryDeletes == 0);
    }
    Reset(); fault = Fault::Verify; failAt = 2;
    auto original = files[L"C:\\Windows\\System32\\forwarders\\user32.dll"].Bytes;
    Check("R18_final_verification_rolls_back",
        !PatchPeFileImports(L"C:\\Windows\\System32\\forwarders\\user32.dll") && faultHit &&
        files[L"C:\\Windows\\System32\\forwarders\\user32.dll"].Bytes == original &&
        !files.count(L"C:\\Windows\\System32\\forwarders\\user32.dll.orig"));
    Reset(); fault = Fault::Rollback;
    original = files[L"C:\\Windows\\System32\\forwarders\\user32.dll"].Bytes;
    Check("R18_failed_rollback_keeps_untouched_backup", Install() != 0 && faultHit && !liveTruncated &&
        files[L"C:\\Windows\\System32\\forwarders\\user32.dll.orig"].Bytes == original);
    Reset(); fault = Fault::RegDelete;
    Check("R02_registry_delete_failure", Install() != 0 && faultHit && registryDeletes == 1);
    Reset(); fault = Fault::RegWrite;
    Check("dispenser_registry_write_failure", Install() != 0 && faultHit && registryDeletes == 0);
    Check("dispenser_failed_new_registration_has_no_values",
        dispenserValues[KEY_WOW64_64KEY].empty() && registryHandles.empty());
    for (int index = 1; index <= 4; ++index) {
        Reset(); SeedDispenserValues();
        auto previous = dispenserValues;
        fault = Fault::RegWrite; failAt = index;
        Check("dispenser_partial_write_restores_both_views",
            Install() != 0 && faultHit && dispenserValues == previous &&
            registryDeletes == 0 && registryHandles.empty());
    }
    for (int index = 1; index <= 8; ++index) {
        Reset(); SeedDispenserValues();
        auto previous = dispenserValues;
        fault = Fault::RegRead; failAt = index;
        Check("dispenser_snapshot_read_failure_preserves_values",
            Install() != 0 && faultHit && dispenserValues == previous && registryHandles.empty());
    }
    Reset(); SeedDispenserValues();
    auto previousRegistry = dispenserValues;
    fault = Fault::RegDelete;
    Check("dispenser_later_install_failure_rolls_back",
        Install() != 0 && faultHit && dispenserValues == previousRegistry && registryHandles.empty());
    Reset(); SeedDispenserValues();
    previousRegistry = dispenserValues;
    replacementRegistryValue = RegistryText(REG_SZ, L"C:\\external\\server.dll");
    replaceRegistryOnFailure = true; fault = Fault::RegWrite; failAt = 2;
    Check("dispenser_rollback_preserves_external_edit",
        Install() != 0 && faultHit &&
        dispenserValues[KEY_WOW64_64KEY][L""] == replacementRegistryValue &&
        dispenserValues[KEY_WOW64_64KEY][L"ThreadingModel"] == previousRegistry[KEY_WOW64_64KEY][L"ThreadingModel"] &&
        dispenserValues[KEY_WOW64_32KEY] == previousRegistry[KEY_WOW64_32KEY] && registryHandles.empty());
    Reset(); SeedDispenserValues();
    dispenserValues[KEY_WOW64_64KEY][L""] = {REG_BINARY, std::vector<BYTE>(1024 * 1024 + 1, 0xA5)};
    previousRegistry = dispenserValues;
    Check("dispenser_oversize_snapshot_fails_without_value_changes",
        Install() != 0 && dispenserValues == previousRegistry && registryHandles.empty());
    for (size_t size : {size_t{0}, size_t{1}, size_t{1024 * 1024}}) {
        Reset(); SeedDispenserValues();
        dispenserValues[KEY_WOW64_64KEY][L""] = {REG_BINARY, std::vector<BYTE>(size, 0xA5)};
        previousRegistry = dispenserValues;
        fault = Fault::RegWrite; failAt = 2;
        Check("dispenser_raw_snapshot_boundary_restored",
            Install() != 0 && faultHit && dispenserValues == previousRegistry && registryHandles.empty());
    }
    Reset(); knownDlls.clear();
    Check("R02_already_absent_values", Install() == 0 && registryDeletes == 6);
    Reset();
    Check("R02_install_success", Install() == 0 && !liveTruncated && knownDlls.empty());
    Check("R02_protected_writes_use_backup_intent", !unsafeFileOpen);
    auto installed = files;
    Check("R02_repeat_install", Install() == 0 && files.size() == installed.size() && !liveTruncated);
    Check("R02_verified_PE32_exports", VerifyPatchedPe(L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll", kUser32Exports));
    Check("R02_verified_PE32_ole32_exports", VerifyPatchedPe(L"C:\\Windows\\SysWOW64\\forwarders\\ole32.dll", kOle32Exports));
    Check("R02_verified_PE32_comctl32_exports", VerifyPatchedPe(L"C:\\Windows\\SysWOW64\\forwarders\\comctl32.dll", kComctl32Exports));
    Check("R02_verified_PE32_shell32_exports", VerifyPatchedPe(L"C:\\Windows\\SysWOW64\\forwarders\\shell32.dll", kShell32Exports));
    Check("R02_verified_PE32_winmm_exports", VerifyPatchedPe(L"C:\\Windows\\SysWOW64\\forwarders\\winmm.dll", kWinmmExports));
    Check("classic_shell_ordinals_verified", VerifyOrdinalForwarders(
        L"C:\\Windows\\SysWOW64\\forwarders\\shlwapi.dll", kShlwapiOrdinals));
    Check("classic_shell_x64_ordinals_verified", VerifyOrdinalForwarders(
        L"C:\\Windows\\System32\\forwarders\\shlwapi.dll", ShlwapiOrdinals(IMAGE_FILE_MACHINE_AMD64)));
    Check("legacy_kernel32_forwarder_PE32", VerifyPatchedPe(
        L"C:\\Windows\\SysWOW64\\forwarders\\kernel32.dll", {"_hread", "QueryActCtxW"}, false));
    Check("legacy_kernel32_forwarder_PE64", VerifyPatchedPe(
        L"C:\\Windows\\System32\\forwarders\\kernel32.dll", {"_hread", "QueryActCtxW"}, false));
    Check("legacy_kernel32_no_root_copy",
        files.count(L"C:\\Windows\\SysWOW64\\kernel32.dll") == 0);
    Check("R02_verified_PE64_exports", VerifyPatchedPe(L"C:\\Windows\\System32\\forwarders\\gdi32.dll", kGdi32Exports));
    files[L"C:\\Windows\\System32\\forwarders\\gdi32.dll"].Bytes[0] = 0;
    Check("R02_verification_rejects_corruption", !VerifyPatchedPe(L"C:\\Windows\\System32\\forwarders\\gdi32.dll", kGdi32Exports));
    Reset();
    for (const wchar_t* dll : kOptionalWow64RuntimeDlls) {
        files[std::wstring(L"C:\\payload\\") + dll] = {Fixture(IMAGE_FILE_MACHINE_I386)};
    }
    bool allRuntimesCopied = (Install() == 0);
    for (const wchar_t* dll : kOptionalWow64RuntimeDlls) {
        allRuntimesCopied = allRuntimesCopied && (files.count(std::wstring(L"C:\\Windows\\SysWOW64\\") + dll) == 1);
    }
    Check("R02_optional_wow64_runtimes_copied", allRuntimesCopied);
    Reset();
    const wchar_t* shell32Target = L"C:\\Windows\\SysWOW64\\forwarders\\shell32.dll";
    *reinterpret_cast<DWORD*>(files[shell32Target].Bytes.data() + 0x640) = 0x20A0;
    memcpy(files[shell32Target].Bytes.data() + 0x680, "SHGetPathFromIDListW", 21);
    memcpy(files[shell32Target].Bytes.data() + 0x6A0, "windows.storage.dll.SHGetPathFromIDListW", 41);
    Check("R02_broken_forwarder_rejected_before_patch", !VerifyPatchedPe(shell32Target, kShell32Exports));
    Check("R02_broken_forwarder_redirected_to_shim",
        PatchPeFileImports(shell32Target) && PatchPeFileExports(shell32Target, kShell32Exports) &&
        VerifyPatchedPe(shell32Target, kShell32Exports));
    Reset();
    const wchar_t* target = L"C:\\Windows\\System32\\forwarders\\user32.dll";
    original = files[target].Bytes;
    auto staged = original;
    files[std::wstring(target) + L".orig"] = {original};
    Check("R18_existing_backup_is_untouched", !WritePeBufferToFile(target, staged, SIZE_MAX) &&
        files[target].Bytes == original && mutations == 0);
    Reset();
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(files[target].Bytes.data() + 0x400);
    imports->Name = 0x23F0;
    memset(files[target].Bytes.data() + 0x9F0, 'X', 16);
    original = files[target].Bytes;
    Check("R18_unterminated_import_name", !PatchPeFileImports(target) && files[target].Bytes == original && mutations == 0);
    Reset();
    auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(files[target].Bytes.data() + 0x600);
    exports->NumberOfFunctions = 0xFFFFFFFF;
    original = files[target].Bytes;
    Check("R18_export_array_bounds", !PatchPeFileExports(target, kUser32Exports) &&
        files[target].Bytes == original && mutations == 0);
    Reset();
    *reinterpret_cast<DWORD*>(files[target].Bytes.data() + 0x644) = 0x23F0;
    memset(files[target].Bytes.data() + 0x9F0, 'X', 16);
    original = files[target].Bytes;
    Check("R18_unterminated_export_name", !PatchPeFileExports(target, kUser32Exports) &&
        files[target].Bytes == original && mutations == 0);
    Reset();
    const wchar_t* shell = L"C:\\Windows\\System32\\forwarders\\shlwapi.dll";
    Check("classic_ordinal_collision_preserves_named_code",
        PatchPeFileExports(shell, {}, {{1, "kernel32.GetTickCount"}}) &&
        NamedExportRva(files[shell].Bytes, "Existing") == 0x1100 &&
        VerifyOrdinalForwarders(shell, {{1, "kernel32.GetTickCount"}}));
    auto unchanged = files[shell].Bytes;
    Check("classic_ordinal_collision_patch_idempotent",
        PatchPeFileExports(shell, {}, {{1, "kernel32.GetTickCount"}}) && unchanged == files[shell].Bytes);
    Reset();
    *reinterpret_cast<DWORD*>(files[shell].Bytes.data() + 0x640) = 0x20A0;
    memcpy(files[shell].Bytes.data() + 0x6A0, "kernel32.GetCurrentProcessId", 28);
    bool patched = PatchPeFileExports(shell, {}, {{1, "kernel32.GetTickCount"}});
    std::string preserved;
    auto* headers = GetPeHeaders(files[shell].Bytes);
    DWORD preservedRva = NamedExportRva(files[shell].Bytes, "Existing");
    Check("classic_ordinal_collision_preserves_named_forwarder", patched &&
        ReadPeString(files[shell].Bytes, RvaToOffset(headers, headers->FileHeader.NumberOfSections, preservedRva), preserved) &&
        preserved == "kernel32.GetCurrentProcessId");
    std::printf("SUMMARY installer failures=%d\n", failures);
    return failures ? 1 : 0;
}
