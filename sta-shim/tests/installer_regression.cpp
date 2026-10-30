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

enum class Fault { None, Copy, CreateStage, Write, ShortWrite, Flush, SetAttributes, MoveOriginal, Replace, Rollback, Verify, RegOpen, RegDelete, Privilege };
struct File { std::vector<BYTE> Bytes; DWORD Attributes = FILE_ATTRIBUTE_NORMAL; bool Written = false; };
struct OpenFile { std::wstring Path; size_t Position = 0; };
std::map<std::wstring, File> files;
std::map<HANDLE, OpenFile> handles;
std::set<std::wstring> directories;
std::set<std::wstring> knownDlls;
Fault fault = Fault::None;
int failAt = 1, faultCalls = 0, copies = 0, registryDeletes = 0, mutations = 0, failures = 0;
bool faultHit = false, liveTruncated = false, unsafeFileOpen = false;
ULONG_PTR nextHandle = 0x10000;

bool EndsWith(const std::wstring& text, const wchar_t* suffix) {
    size_t length = wcslen(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

bool Fail(Fault operation) {
    if (fault != operation || ++faultCalls != failAt) return false;
    faultHit = true;
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
            if (Fail(Fault::CreateStage)) return INVALID_HANDLE_VALUE;
        } else {
            ++copies;
            if (Fail(Fault::Copy)) return INVALID_HANDLE_VALUE;
        }
    }
    if (disposition == CREATE_ALWAYS && !EndsWith(path, L".new")) liveTruncated = true;
    if (disposition == CREATE_ALWAYS || disposition == CREATE_NEW) { files[path] = {}; ++mutations; }
    HANDLE handle = reinterpret_cast<HANDLE>(++nextHandle);
    handles[handle] = {path, 0};
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
BOOL WINAPI FakeClose(HANDLE handle) { handles.erase(handle); return TRUE; }

BOOL WINAPI FakeSetAttributes(LPCWSTR path, DWORD attributes) {
    if (Fail(Fault::SetAttributes)) return FALSE;
    auto found = files.find(path);
    if (found == files.end()) return FALSE;
    found->second.Attributes = attributes;
    ++mutations;
    return TRUE;
}

BOOL WINAPI FakeDelete(LPCWSTR path) {
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
    auto found = handles.find(handle);
    if (found == handles.end()) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (type == FileBasicInfo) {
        return FakeSetAttributes(found->second.Path.c_str(), static_cast<FILE_BASIC_INFO*>(input)->FileAttributes);
    }
    if (type == FileDispositionInfo) {
        return static_cast<FILE_DISPOSITION_INFO*>(input)->DeleteFile &&
            FakeDelete(found->second.Path.c_str());
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
LSTATUS WINAPI FakeRegCreate(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD options, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY key, LPDWORD) {
    if (options != REG_OPTION_BACKUP_RESTORE) return ERROR_ACCESS_DENIED;
    *key = reinterpret_cast<HKEY>(2);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI FakeRegDelete(HKEY, LPCWSTR name) {
    ++registryDeletes;
    if (Fail(Fault::RegDelete)) return ERROR_ACCESS_DENIED;
    if (!knownDlls.erase(name)) return ERROR_FILE_NOT_FOUND;
    ++mutations;
    return ERROR_SUCCESS;
}
LSTATUS WINAPI FakeRegClose(HKEY) { return ERROR_SUCCESS; }
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
#define OpenProcessToken FakeOpenToken
#define LookupPrivilegeValueW FakeLookupPrivilege
#define AdjustTokenPrivileges FakeAdjustToken
#define RegOpenKeyExW FakeRegOpen
#define RegCreateKeyExW FakeRegCreate
#define RegDeleteValueW FakeRegDelete
#define RegCloseKey FakeRegClose
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

void Reset(bool wow64 = true) {
    files.clear(); handles.clear(); directories.clear();
    knownDlls = {L"user32", L"ole32", L"gdi32"};
    fault = Fault::None; failAt = 1; faultCalls = copies = registryDeletes = mutations = 0;
    faultHit = liveTruncated = unsafeFileOpen = false;
    directories.insert(L"C:\\payload");
    files[L"C:\\payload\\nanoserver_sta_shim_x64.dll"] = {Fixture(IMAGE_FILE_MACHINE_AMD64)};
    for (const std::wstring& system : {std::wstring(L"C:\\Windows\\System32"), std::wstring(L"C:\\Windows\\SysWOW64")}) {
        bool isWow = system.find(L"SysWOW64") != std::wstring::npos;
        if (isWow && !wow64) continue;
        WORD machine = isWow ? IMAGE_FILE_MACHINE_I386 : IMAGE_FILE_MACHINE_AMD64;
        directories.insert(system);
        directories.insert(system + L"\\forwarders");
        for (const wchar_t* name : {L"user32.dll", L"ole32.dll", L"gdi32.dll", L"comctl32.dll"}) {
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

} // namespace

int main() {
    Reset(false);
    Check("R02_x64_without_x86_payload", Install(false) == 0 && registryDeletes == 3);
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
    Reset(); knownDlls.clear();
    Check("R02_already_absent_values", Install() == 0 && registryDeletes == 3);
    Reset();
    Check("R02_install_success", Install() == 0 && !liveTruncated && knownDlls.empty());
    Check("R02_protected_writes_use_backup_intent", !unsafeFileOpen);
    auto installed = files;
    Check("R02_repeat_install", Install() == 0 && files.size() == installed.size() && !liveTruncated);
    Check("R02_verified_PE32_exports", VerifyPatchedPe(L"C:\\Windows\\SysWOW64\\forwarders\\user32.dll", kUser32Exports));
    Check("R02_verified_PE64_exports", VerifyPatchedPe(L"C:\\Windows\\System32\\forwarders\\gdi32.dll", kGdi32Exports));
    files[L"C:\\Windows\\System32\\forwarders\\gdi32.dll"].Bytes[0] = 0;
    Check("R02_verification_rejects_corruption", !VerifyPatchedPe(L"C:\\Windows\\System32\\forwarders\\gdi32.dll", kGdi32Exports));
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
    std::printf("SUMMARY installer failures=%d\n", failures);
    return failures ? 1 : 0;
}
