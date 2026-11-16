// install_sta_shim.cpp
// Installs nanoserver_sta_shim.dll and patches the user32, ole32, gdi32, and comctl32
// forwarders on Windows Nano Server, including missing export forwarders. Every required
// operation and on-disk verification must succeed before installation reports success.

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

bool IsBufferRange(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

bool ReadFileBuffer(const wchar_t* path, std::vector<BYTE>& bytes) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > MAXDWORD) {
        CloseHandle(file);
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    DWORD error = GetLastError();
    CloseHandle(file);
    if (!success || read != bytes.size()) {
        SetLastError(success ? ERROR_READ_FAULT : error);
        return false;
    }
    return true;
}

IMAGE_NT_HEADERS* GetPeHeaders(std::vector<BYTE>& bytes) {
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) return nullptr;
    size_t offset = static_cast<size_t>(dos->e_lfanew);
    if (!IsBufferRange(bytes.size(), offset, sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + sizeof(WORD))) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(bytes.data() + offset);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    size_t optionalSize = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ? sizeof(IMAGE_OPTIONAL_HEADER32) :
                          nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? sizeof(IMAGE_OPTIONAL_HEADER64) : 0;
    if (!optionalSize || nt->FileHeader.SizeOfOptionalHeader < optionalSize ||
        !IsBufferRange(bytes.size(), offset + 24, nt->FileHeader.SizeOfOptionalHeader +
            static_cast<size_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER))) return nullptr;
    return nt;
}

bool PeRvaRange(const std::vector<BYTE>& bytes, IMAGE_NT_HEADERS* nt, DWORD rva,
                DWORD length, size_t& offset) {
    DWORD headersSize = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.SizeOfHeaders :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.SizeOfHeaders;
    if (rva < headersSize && IsBufferRange(headersSize, rva, length)) {
        offset = rva;
        return IsBufferRange(bytes.size(), offset, length);
    }
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        if (rva < section.VirtualAddress) continue;
        DWORD delta = rva - section.VirtualAddress;
        DWORD span = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        if (delta >= span) continue;
        if (!IsBufferRange(section.SizeOfRawData, delta, length) ||
            !IsBufferRange(bytes.size(), section.PointerToRawData, section.SizeOfRawData)) return false;
        offset = static_cast<size_t>(section.PointerToRawData) + delta;
        return true;
    }
    return false;
}

bool ClearModifiedImageHotpatchCompatibility(std::vector<BYTE>& bytes, bool& modified) {
    modified = false;
    auto invalid = [] { SetLastError(ERROR_BAD_EXE_FORMAT); return false; };
    auto* nt = GetPeHeaders(bytes);
    if (!nt) return invalid();
    bool is32 = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    auto* pe32 = reinterpret_cast<IMAGE_NT_HEADERS32*>(nt);
    auto* pe64 = reinterpret_cast<IMAGE_NT_HEADERS64*>(nt);
    DWORD count = is32 ? pe32->OptionalHeader.NumberOfRvaAndSizes : pe64->OptionalHeader.NumberOfRvaAndSizes;
    if (count <= IMAGE_DIRECTORY_ENTRY_DEBUG) return true;
    auto* directories = is32 ? pe32->OptionalHeader.DataDirectory : pe64->OptionalHeader.DataDirectory;
    const auto& directory = directories[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!directory.VirtualAddress && !directory.Size) return true;
    size_t offset = 0;
    if (!directory.VirtualAddress || !directory.Size || directory.Size % sizeof(IMAGE_DEBUG_DIRECTORY) ||
        !PeRvaRange(bytes, nt, directory.VirtualAddress, directory.Size, offset)) return invalid();
    std::vector<size_t> characteristics;
    for (size_t entry = 0; entry < directory.Size; entry += sizeof(IMAGE_DEBUG_DIRECTORY)) {
        IMAGE_DEBUG_DIRECTORY debug{};
        memcpy(&debug, bytes.data() + offset + entry, sizeof(debug));
        if (debug.Type != IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS) continue;
        if (debug.SizeOfData < sizeof(DWORD) || !debug.PointerToRawData ||
            !IsBufferRange(bytes.size(), debug.PointerToRawData, debug.SizeOfData)) return invalid();
        size_t mapped = 0;
        if (debug.AddressOfRawData &&
            (!PeRvaRange(bytes, nt, debug.AddressOfRawData, debug.SizeOfData, mapped) ||
             mapped != debug.PointerToRawData)) return invalid();
        characteristics.push_back(debug.PointerToRawData);
    }
    // Rewritten OS images no longer match Microsoft's hotpatch baseline. Retaining
    // this bit also reserves an image-extension tail that can crowd kernel32's
    // ASLR address during parallel WOW64 startup. Keep CET, ASLR, NX and CFG intact.
    for (size_t data : characteristics) {
        DWORD flags = 0;
        memcpy(&flags, bytes.data() + data, sizeof(flags));
        if (!(flags & IMAGE_DLLCHARACTERISTICS_EX_HOTPATCH_COMPATIBLE)) continue;
        flags &= ~IMAGE_DLLCHARACTERISTICS_EX_HOTPATCH_COMPATIBLE;
        memcpy(bytes.data() + data, &flags, sizeof(flags));
        modified = true;
    }
    if (modified) {
        if (is32) pe32->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
        else pe64->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
        directories[IMAGE_DIRECTORY_ENTRY_SECURITY] = {};
    }
    return true;
}

bool ReadPeString(const std::vector<BYTE>& bytes, DWORD offset, std::string& output) {
    if (!offset || offset >= bytes.size()) return false;
    const char* start = reinterpret_cast<const char*>(bytes.data() + offset);
    const char* end = static_cast<const char*>(memchr(start, 0, bytes.size() - offset));
    if (!end) return false;
    output.assign(start, end);
    return true;
}

bool VerifyFileBytes(const wchar_t* path, const std::vector<BYTE>& expected) {
    std::vector<BYTE> actual;
    if (!ReadFileBuffer(path, actual)) return false;
    if (actual != expected) {
        SetLastError(ERROR_WRITE_FAULT);
        return false;
    }
    return true;
}

bool SetFileInformationForRestore(const wchar_t* path, DWORD access, FILE_INFO_BY_HANDLE_CLASS type,
                                 void* information, DWORD size, LONG* nativeStatus = nullptr) {
    using LastStatusFn = LONG (NTAPI*)();
    LastStatusFn lastStatus = nullptr;
    if (nativeStatus) {
        *nativeStatus = 0;
        lastStatus = reinterpret_cast<LastStatusFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetLastNtStatus"));
    }
    HANDLE file = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BOOL success = SetFileInformationByHandle(file, type, information, size);
    DWORD error = GetLastError();
    if (!success && nativeStatus && lastStatus) *nativeStatus = lastStatus();
    if (!success && !nativeStatus)
        fwprintf(stderr, L"[ERR] File metadata update failed (class=%d): %s (err=%lu).\n",
                 static_cast<int>(type), path, error);
    if (!CloseHandle(file)) {
        if (success) error = GetLastError();
        success = FALSE;
    }
    if (!success) SetLastError(error);
    return success != FALSE;
}

bool SetFileAttributesForRestore(const wchar_t* path, DWORD attributes) {
    FILE_BASIC_INFO information{};
    information.FileAttributes = attributes;
    return SetFileInformationForRestore(path, FILE_WRITE_ATTRIBUTES, FileBasicInfo,
                                        &information, sizeof(information));
}

bool UnlinkMappedImageForRestore(const wchar_t* path) {
    HANDLE original = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (original == INVALID_HANDLE_VALUE) return false;
    std::wstring markerPath = std::wstring(path) + L".unlink";
    HANDLE marker = INVALID_HANDLE_VALUE;
    bool success = [&] {
        FILE_BASIC_INFO basic{};
        if (!GetFileInformationByHandleEx(original, FileBasicInfo, &basic, sizeof(basic))) return false;
        if (basic.FileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) {
            SetLastError(ERROR_ACCESS_DENIED);
            return false;
        }
        marker = CreateFileW(markerPath.c_str(), GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (marker == INVALID_HANDLE_VALUE) return false;
        FILE_DISPOSITION_INFO_EX onClose{FILE_DISPOSITION_FLAG_DELETE |
            FILE_DISPOSITION_FLAG_POSIX_SEMANTICS | FILE_DISPOSITION_FLAG_ON_CLOSE};
        if (!SetFileInformationByHandle(marker, FileDispositionInfoEx, &onClose, sizeof(onClose))) return false;

        // Replace only the cleanup name. The original image stream remains
        // available to existing views; the owned empty replacement dies on close.
        std::wstring target = path;
        std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t), 0);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        rename->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
        rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
        memcpy(rename->FileName, target.c_str(), (target.size() + 1) * sizeof(wchar_t));
        return SetFileInformationByHandle(marker, FileRenameInfoEx, rename, static_cast<DWORD>(buffer.size())) != FALSE;
    }();
    DWORD error = GetLastError();
    if (marker != INVALID_HANDLE_VALUE && !CloseHandle(marker)) {
        if (success) error = GetLastError();
        success = false;
    }
    if (!CloseHandle(original)) {
        if (success) error = GetLastError();
        success = false;
    }
    if (!success) { SetLastError(error); return false; }
    const wchar_t* removed[] = {path, markerPath.c_str()};
    for (const wchar_t* name : removed) {
        if (GetFileAttributesW(name) != INVALID_FILE_ATTRIBUTES) {
            SetLastError(ERROR_FILE_EXISTS);
            return false;
        }
        DWORD missing = GetLastError();
        if (missing != ERROR_FILE_NOT_FOUND && missing != ERROR_PATH_NOT_FOUND) return false;
    }
    fwprintf(stdout, L"[OK] Removed mapped image backup by POSIX replacement: %s\n", path);
    return true;
}

bool DeleteFileForRestore(const wchar_t* path) {
    FILE_DISPOSITION_INFO_EX information{
        FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS};
    LONG status = 0;
    if (SetFileInformationForRestore(path, DELETE, FileDispositionInfoEx,
                                     &information, sizeof(information), &status)) return true;
    // Container file systems can still reject POSIX disposition for an image.
    // Do not retry permission failures or ignore the read-only attribute.
    constexpr LONG cannotDelete = static_cast<LONG>(0xC0000121u);
    if (status == cannotDelete && UnlinkMappedImageForRestore(path)) return true;
    DWORD error = GetLastError();
    fwprintf(stderr, L"[ERR] File removal failed: %s (err=%lu, status=0x%08lX).\n",
             path, error, static_cast<ULONG>(status));
    SetLastError(error);
    return false;
}

bool MoveFileForRestore(const wchar_t* source, const wchar_t* destination) {
    std::wstring target = destination;
    std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t), 0);
    auto* information = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    information->ReplaceIfExists = FALSE;
    // Nano's Win32 rename wrapper rejects a relative name plus RootDirectory.
    information->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
    memcpy(information->FileName, target.c_str(), (target.size() + 1) * sizeof(wchar_t));
    return SetFileInformationForRestore(source, DELETE, FileRenameInfo,
                                        information, static_cast<DWORD>(buffer.size()));
}

bool WriteNewFileBuffer(const wchar_t* path, const std::vector<BYTE>& bytes) {
    // Enabling SeRestorePrivilege is not enough: protected Nano directories also
    // require backup intent on the file and metadata handles. Keep their ACLs intact.
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    LARGE_INTEGER size{};
    BOOL success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    DWORD error = success ? ERROR_WRITE_FAULT : GetLastError();
    if (success && written == bytes.size()) {
        success = FlushFileBuffers(file);
        if (!success) error = GetLastError();
    } else success = FALSE;
    if (success) {
        success = GetFileSizeEx(file, &size);
        if (!success) error = GetLastError();
        else if (size.QuadPart != bytes.size()) { success = FALSE; error = ERROR_WRITE_FAULT; }
    }
    if (!CloseHandle(file)) { success = FALSE; error = GetLastError(); }
    if (success && !VerifyFileBytes(path, bytes)) { success = FALSE; error = GetLastError(); }
    if (!success) {
        DeleteFileForRestore(path);
        SetLastError(error);
    }
    return success != FALSE;
}

bool WritePeBufferToFile(const wchar_t* path, std::vector<BYTE>& bytes, size_t checksumOffset) {
    if (checksumOffset != SIZE_MAX) {
        if (!IsBufferRange(bytes.size(), checksumOffset, sizeof(DWORD))) return false;
        *reinterpret_cast<DWORD*>(bytes.data() + checksumOffset) = ComputePeChecksum(bytes.data(), bytes.size(), checksumOffset);
    }
    std::wstring staged = std::wstring(path) + L".new";
    std::wstring backup = std::wstring(path) + L".orig";
    if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"[ERR] Backup already exists; preserving it: %s\n", backup.c_str());
        SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }
    if (!WriteNewFileBuffer(staged.c_str(), bytes)) return false;
    DWORD error = ERROR_SUCCESS;
    DWORD attributes = GetFileAttributesW(path);
    DWORD writableAttributes = attributes & ~FILE_ATTRIBUTE_READONLY;
    if (!writableAttributes) writableAttributes = FILE_ATTRIBUTE_NORMAL;
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !SetFileAttributesForRestore(path, writableAttributes) ||
        !MoveFileForRestore(path, backup.c_str())) {
        error = GetLastError();
        DeleteFileForRestore(staged.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) SetFileAttributesForRestore(path, attributes);
        SetLastError(error);
        return false;
    }
    if (!MoveFileForRestore(staged.c_str(), path)) {
        error = GetLastError();
        if (!MoveFileForRestore(backup.c_str(), path)) {
            fwprintf(stderr, L"[ERR] Rollback failed; untouched original remains at %s (err=%lu).\n", backup.c_str(), GetLastError());
        } else {
            SetFileAttributesForRestore(path, attributes);
        }
        DeleteFileForRestore(staged.c_str());
        SetLastError(error);
        return false;
    }
    if (!VerifyFileBytes(path, bytes)) {
        error = GetLastError();
        if (!MoveFileForRestore(path, staged.c_str()) ||
            !MoveFileForRestore(backup.c_str(), path)) {
            fwprintf(stderr, L"[ERR] Verification rollback failed; original is at %s.\n", backup.c_str());
        } else {
            SetFileAttributesForRestore(path, attributes);
            DeleteFileForRestore(staged.c_str());
        }
        SetLastError(error);
        return false;
    }
    if (!SetFileAttributesForRestore(path, attributes) || !DeleteFileForRestore(backup.c_str())) return false;
    return true;
}
bool PatchPeFileImports(const wchar_t* filePath, std::vector<BYTE>* prepared = nullptr) {
    std::vector<BYTE> storage;
    auto& buf = prepared ? *prepared : storage;
    if (!prepared && !ReadFileBuffer(filePath, buf)) return false;
    auto* nt = GetPeHeaders(buf);
    if (!nt) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return false;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
    bool metadataModified = false;
    if (!ClearModifiedImageHotpatchCompatibility(buf, metadataModified)) return false;

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
    for (size_t offset = impOff; ; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        if (!IsBufferRange(buf.size(), offset, sizeof(IMAGE_IMPORT_DESCRIPTOR))) return false;
        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(buf.data() + offset);
        if (!desc->Name && !desc->FirstThunk) break;
        DWORD nameOff = RvaToOffset(nt, nt->FileHeader.NumberOfSections, desc->Name);
        std::string name;
        if (!ReadPeString(buf, nameOff, name)) return false;
        char* dllName = reinterpret_cast<char*>(buf.data() + nameOff);
        if (_stricmp(name.c_str(), "api-ms-win-core-apiquery-l1-1-0.dll") == 0 ||
            _stricmp(name.c_str(), "api-ms-win-core-apiquery-l1-1-1.dll") == 0 ||
            _stricmp(name.c_str(), "nanoserver_sta_shim.dll") == 0) {
            const char* replacement = "nanoserver_sta_shim.dll";
            size_t origLen = name.size();
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

    if (prepared) return true;
    if (!WritePeBufferToFile(filePath, buf, checksumOffset)) return false;
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

struct OrdinalExport {
    WORD Ordinal;
    const char* Target;
    const char* Name = nullptr;
};

bool PatchPeFileExports(const wchar_t* filePath, const std::vector<const char*>& missingExports,
                        const std::vector<OrdinalExport>& ordinalExports = {}, bool replaceCode = false,
                        std::vector<BYTE>* prepared = nullptr) {
    if (missingExports.empty() && ordinalExports.empty()) {
        return true;
    }

    std::vector<BYTE> storage;
    auto& buf = prepared ? *prepared : storage;
    if (!prepared && !ReadFileBuffer(filePath, buf)) return false;
    auto* nt = GetPeHeaders(buf);
    if (!nt) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return false;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
    bool metadataModified = false;
    if (!ClearModifiedImageHotpatchCompatibility(buf, metadataModified)) return false;

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
    if (!IsBufferRange(buf.size(), funcsOff, static_cast<size_t>(origExpDir.NumberOfFunctions) * sizeof(DWORD)) ||
        !IsBufferRange(buf.size(), namesOff, static_cast<size_t>(origExpDir.NumberOfNames) * sizeof(DWORD)) ||
        !IsBufferRange(buf.size(), ordsOff, static_cast<size_t>(origExpDir.NumberOfNames) * sizeof(WORD)) ||
        origExpDir.NumberOfFunctions > 0xFFFFu) return false;
    std::string dllNameStr;
    if (!ReadPeString(buf, dllNameOff, dllNameStr)) return false;
    const DWORD* origFuncs = reinterpret_cast<const DWORD*>(buf.data() + funcsOff);
    const DWORD* origNames = reinterpret_cast<const DWORD*>(buf.data() + namesOff);
    const WORD* origOrds = reinterpret_cast<const WORD*>(buf.data() + ordsOff);

    std::vector<DWORD> newFunctions(origFuncs, origFuncs + origExpDir.NumberOfFunctions);
    std::vector<NamedExportEntry> namedExports;
    namedExports.reserve(origExpDir.NumberOfNames + missingExports.size());

    for (DWORD i = 0; i < origExpDir.NumberOfNames; ++i) {
        DWORD nameOff = RvaToOffset(nt, numSections, origNames[i]);
        std::string name;
        if (!ReadPeString(buf, nameOff, name) || origOrds[i] >= origExpDir.NumberOfFunctions) return false;
        namedExports.push_back(NamedExportEntry{name, origOrds[i]});
    }

    std::vector<ForwarderFixup> forwarderFixups;
    for (DWORD i = 0; i < origExpDir.NumberOfFunctions; ++i) {
        DWORD fnRva = origFuncs[i];
        if (fnRva >= expRva && fnRva < expRva + expSize) {
            DWORD fwdOff = RvaToOffset(nt, numSections, fnRva);
            std::string target;
            if (!ReadPeString(buf, fwdOff, target)) return false;
            forwarderFixups.push_back(ForwarderFixup{static_cast<WORD>(i), target});
        }
    }

    size_t addedCount = 0;
    for (const char* missingName : missingExports) {
        bool exists = false;
        WORD existingOrdIdx = 0;
        for (const auto& entry : namedExports) {
            if (entry.Name == missingName) {
                exists = true;
                existingOrdIdx = entry.OrdinalIndex;
                break;
            }
        }
        if (!exists) {
            if (newFunctions.size() >= 0x10000u) return false;
            WORD newOrdIdx = static_cast<WORD>(newFunctions.size());
            newFunctions.push_back(0);
            namedExports.push_back(NamedExportEntry{std::string(missingName), newOrdIdx});
            forwarderFixups.push_back(ForwarderFixup{
                newOrdIdx,
                std::string("nanoserver_sta_shim.") + missingName});
            ++addedCount;
        } else {
            const std::string shimTarget = std::string("nanoserver_sta_shim.") + missingName;
            bool hasForwarder = false;
            for (auto& fixup : forwarderFixups) {
                if (fixup.OrdinalIndex == existingOrdIdx) {
                    hasForwarder = true;
                    if (fixup.Target != shimTarget) { fixup.Target = shimTarget; ++addedCount; }
                    break;
                }
            }
            if (!hasForwarder && replaceCode) {
                forwarderFixups.push_back(ForwarderFixup{existingOrdIdx, shimTarget});
                ++addedCount;
            }
        }
    }

    // Reserve classic ordinal slots before relocating aliases, so a preserved
    // named export cannot be displaced again by a later ordinal in this pass.
    for (const auto& entry : ordinalExports) {
        if (entry.Ordinal < origExpDir.Base || entry.Ordinal - origExpDir.Base >= 0x10000u) return false;
        WORD index = static_cast<WORD>(entry.Ordinal - origExpDir.Base);
        if (index >= newFunctions.size()) newFunctions.resize(static_cast<size_t>(index) + 1, 0);
    }
    for (const auto& entry : ordinalExports) {
        WORD index = static_cast<WORD>(entry.Ordinal - origExpDir.Base);
        bool hasAliases = std::any_of(namedExports.begin(), namedExports.end(),
            [&](const NamedExportEntry& item) {
                return item.OrdinalIndex == index && (!entry.Name || item.Name != entry.Name);
            });
        if (hasAliases) {
            if (newFunctions.size() >= 0x10000u) return false;
            WORD preserved = static_cast<WORD>(newFunctions.size());
            newFunctions.push_back(newFunctions[index]);
            auto previous = std::find_if(forwarderFixups.begin(), forwarderFixups.end(),
                [index](const ForwarderFixup& item) { return item.OrdinalIndex == index; });
            if (previous != forwarderFixups.end()) {
                std::string target = previous->Target;
                forwarderFixups.push_back(ForwarderFixup{preserved, target});
            }
            for (auto& item : namedExports)
                if (item.OrdinalIndex == index && (!entry.Name || item.Name != entry.Name))
                    item.OrdinalIndex = preserved;
            ++addedCount;
        }
        if (entry.Name) {
            auto named = std::find_if(namedExports.begin(), namedExports.end(),
                [&](const NamedExportEntry& item) { return item.Name == entry.Name; });
            if (named == namedExports.end()) {
                namedExports.push_back(NamedExportEntry{entry.Name, index});
                ++addedCount;
            } else if (named->OrdinalIndex != index) {
                named->OrdinalIndex = index;
                ++addedCount;
            }
        }
        auto found = std::find_if(forwarderFixups.begin(), forwarderFixups.end(),
            [index](const ForwarderFixup& item) { return item.OrdinalIndex == index; });
        if (found == forwarderFixups.end()) {
            forwarderFixups.push_back(ForwarderFixup{index, entry.Target});
            ++addedCount;
        } else if (found->Target != entry.Target) {
            found->Target = entry.Target;
            ++addedCount;
        }
    }

    if (addedCount == 0) {
        if (prepared) return true;
        if (metadataModified && !WritePeBufferToFile(filePath, buf, checksumOffset)) return false;
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

    if (prepared) return true;
    if (!WritePeBufferToFile(filePath, buf, checksumOffset)) {
        return false;
    }

    wprintf(L"[OK] Added %zu missing export(s) to: %s\n", addedCount, filePath);
    return true;
}

bool PatchPeFileImportsAndExports(const wchar_t* path, const std::vector<const char*>& exports,
                                 const std::vector<OrdinalExport>& ordinals = {}, bool replaceCode = false) {
    std::vector<BYTE> bytes;
    if (!ReadFileBuffer(path, bytes)) return false;
    const auto original = bytes;
    // Never publish an import-only intermediate image. A loader can map it
    // before the export pass, preventing cleanup of that second replacement.
    if (!PatchPeFileImports(path, &bytes) ||
        !PatchPeFileExports(path, exports, ordinals, replaceCode, &bytes)) return false;
    if (bytes == original) {
        wprintf(L"[OK] Imports and exports already current in: %s\n", path);
        return true;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    size_t checksumOffset = static_cast<size_t>(dos->e_lfanew) + 24 + 64;
    if (!WritePeBufferToFile(path, bytes, checksumOffset)) return false;
    wprintf(L"[OK] Patched imports and exports in: %s\n", path);
    return true;
}

bool EnablePrivilege(LPCWSTR name) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool success = LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid) != FALSE;
    if (success) {
        SetLastError(ERROR_SUCCESS);
        success = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr) &&
                  GetLastError() == ERROR_SUCCESS;
    }
    DWORD error = GetLastError();
    CloseHandle(token);
    SetLastError(error);
    return success;
}

bool OpenKnownDlls(HKEY* key) {
    const wchar_t* path = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\KnownDLLs";
    HKEY existing = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &existing);
    if (status == ERROR_SUCCESS) status = RegCloseKey(existing);
    if (status == ERROR_SUCCESS) {
        // This protected key requires the enabled backup/restore privileges.
        // RegCreateKeyEx documents REG_OPTION_BACKUP_RESTORE; RegOpenKeyEx does not.
        status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, REG_OPTION_BACKUP_RESTORE,
            KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, key, nullptr);
    }
    if (status != ERROR_SUCCESS) {
        SetLastError(status);
        return false;
    }
    return true;
}

bool RemoveKnownDllEntries() {
    HKEY key = nullptr;
    if (!OpenKnownDlls(&key)) return false;
    for (const wchar_t* name : {L"user32", L"ole32", L"gdi32", L"SHELL32", L"gdiplus", L"shlwapi"}) {
        LSTATUS status = RegDeleteValueW(key, name);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            RegCloseKey(key);
            SetLastError(status);
            return false;
        }
    }
    LSTATUS status = RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        SetLastError(status);
        return false;
    }
    wprintf(L"[OK] Updated KnownDLLs registry key.\n");
    return true;
}

bool CopyRequiredFile(const std::wstring& source, const std::wstring& destination) {
    std::vector<BYTE> expected;
    if (!ReadFileBuffer(source.c_str(), expected)) return false;
    std::vector<BYTE> existing;
    bool exists = GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (exists && ReadFileBuffer(destination.c_str(), existing) && existing == expected) return true;
    bool copied = exists ? WritePeBufferToFile(destination.c_str(), expected, SIZE_MAX) :
        WriteNewFileBuffer(destination.c_str(), expected);
    if (!copied || !VerifyFileBytes(destination.c_str(), expected)) {
        fwprintf(stderr, L"[ERR] Copy failed: %s -> %s (err=%lu)\n", source.c_str(), destination.c_str(), GetLastError());
        return false;
    }
    return true;
}

const std::vector<const char*> kGdi32Exports = {
    "GetTextExtentExPointA",
    "CopyEnhMetaFileA", "CopyEnhMetaFileW", "GetEnhMetaFileDescriptionA", "GetEnhMetaFileDescriptionW",
    "GetTextCharacterExtra", "CreatePenIndirect", "GetTextExtentPoint32A", "SetWinMetaFileBits",
    "CopyMetaFileA", "CreateBitmapIndirect", "CreateDiscardableBitmap", "CreateEllipticRgnIndirect",
    "CreateHalftonePalette", "CreateICA", "CreatePolygonRgn", "EnumFontsA",
    "Escape", "GetBrushOrgEx", "GetNearestColor", "GetTextExtentPointA",
    "Pie", "PtInRegion", "ResetDCA", "RoundRect",
    "ScaleViewportExtEx", "ScaleWindowExtEx", "SetAbortProc", "SetBrushOrgEx",
    "SetPixelV", "SetWindowOrgEx", "UnrealizeObject", "WidenPath",
    "AbortPath", "AngleArc", "ArcTo", "Chord",
    "CombineTransform", "CopyMetaFileW", "CreateEllipticRgn", "DrawEscape",
    "EnumEnhMetaFile", "ExtFloodFill", "FlattenPath", "FrameRgn",
    "GdiComment", "GetArcDirection", "GetBoundsRect", "GetDCDpiScaleValue",
    "GetGraphicsMode", "GetMiterLimit", "GetPath", "GetPolyFillMode",
    "GetStretchBltMode", "GetSystemPaletteUse", "IsValidEnhMetaRecordOffExt", "ModifyWorldTransform",
    "OffsetClipRgn", "PlayEnhMetaFileRecord", "PlayMetaFileRecord", "PolyBezier",
    "PolyDraw", "PolyPolygon", "PolyPolyline", "PolylineTo",
    "PtVisible", "ResizePalette", "ScaleRgn", "SetArcDirection",
    "SetBitmapBits", "SetColorAdjustment", "SetMapperFlags", "SetMiterLimit",
    "SetPaletteEntries", "SetTextJustification"
};
const std::vector<const char*> kUser32Exports = {
    "CreateIcon", "GetKeyNameTextA", "GetClassInfoW", "GetClassInfoExW", "CallWindowProcW",
    "DdeAccessData", "DdeClientTransaction", "DdeCmpStringHandles", "DdeConnect",
    "DdeCreateDataHandle", "DdeCreateStringHandleA", "DdeDisconnect", "DdeFreeDataHandle",
    "DdeFreeStringHandle", "DdeGetData", "DdeGetLastError", "DdeInitializeA",
    "DdeNameService", "DdePostAdvise", "DdeQueryConvInfo", "DdeQueryStringA",
    "DdeSetUserHandle", "DdeUninitialize", "MapVirtualKeyExA",
    "LoadCursorFromFileA", "LoadCursorFromFileW", "SetGestureConfig"
};
const std::vector<const char*> kOle32Exports = {
    "CoInitialize", "OleInitialize", "OleUninitialize", "OleRun", "OleLockRunning",
    "OleSaveToStream", "OleLoadFromStream", "CreateDataAdviseHolder", "ReleaseStgMedium",
    "CoIsOle1Class", "IsEqualGUID", "OleConvertIStorageToOLESTREAM", "OleConvertOLESTREAMToIStorage",
    "OleCreateLink", "OleDoAutoConvert"
};
const std::vector<const char*> kComctl32Exports = {
    "ImageList_GetIconSize", "ImageList_SetIconSize",
    "InitializeFlatSB", "UninitializeFlatSB",
    "FlatSB_EnableScrollBar", "FlatSB_ShowScrollBar",
    "FlatSB_GetScrollRange", "FlatSB_GetScrollInfo", "FlatSB_GetScrollPos", "FlatSB_GetScrollProp",
    "FlatSB_SetScrollPos", "FlatSB_SetScrollInfo", "FlatSB_SetScrollRange", "FlatSB_SetScrollProp"
};
const std::vector<const char*> kShell32Exports = {
    "DragQueryFileW", "SHGetPathFromIDListW", "SHGetSpecialFolderLocation"
};
const std::vector<const char*> kWinmmExports = {
    "PlaySoundA", "sndPlaySoundA", "sndPlaySoundW"
};
const std::vector<const char*> kShlwapiExports = {"StrCpyW", "StrCatW", "StrCpyNW", "StrNCatW"};
// Classic Windows ABI, including NONAME exports. Never append these at new ordinals.
const std::vector<OrdinalExport> kShlwapiOrdinals = {
    {28, "user32.IsCharAlphaNumericW"}, {38, "user32.CharLowerW"},
    {44, "user32.CharUpperBuffW"}, {45, "kernel32.CompareStringW"},
    {52, "kernel32.CreateFileW"}, {66, "kernel32.FindResourceW"},
    {115, "kernel32.OutputDebugStringW"}, {159, "kernel32.CompareStringW"},
    {310, "kernel32.GetTimeFormatW"}, {311, "kernel32.GetDateFormatW"},
    {838, "nanoserver_sta_shim.StrCatW", "StrCatW"},
    {852, "nanoserver_sta_shim.StrCpyNW", "StrCpyNW"},
    {853, "nanoserver_sta_shim.StrCpyW", "StrCpyW"},
    {867, "nanoserver_sta_shim.StrNCatW", "StrNCatW"}
};
const std::vector<OrdinalExport>& ShlwapiOrdinals(WORD machine) {
    // The NONAME ordinals are shared. The named helpers differ on x64.
    static const std::vector<OrdinalExport> x64 = [] {
        std::vector<OrdinalExport> exports;
        for (const auto& entry : kShlwapiOrdinals) if (!entry.Name) exports.push_back(entry);
        exports.insert(exports.end(), {
            {843, "nanoserver_sta_shim.StrCatW", "StrCatW"},
            {857, "nanoserver_sta_shim.StrCpyNW", "StrCpyNW"},
            {858, "nanoserver_sta_shim.StrCpyW", "StrCpyW"},
            {872, "nanoserver_sta_shim.StrNCatW", "StrNCatW"}
        });
        return exports;
    }();
    return machine == IMAGE_FILE_MACHINE_AMD64 ? x64 : kShlwapiOrdinals;
}

const std::vector<const char*> kNoExports;

const std::vector<const char*>& RequiredExports(const std::wstring& name) {
    if (name == L"user32.dll") return kUser32Exports;
    if (name == L"gdi32.dll") return kGdi32Exports;
    if (name == L"ole32.dll") return kOle32Exports;
    if (name == L"comctl32.dll") return kComctl32Exports;
    if (name == L"shell32.dll") return kShell32Exports;
    if (name == L"winmm.dll") return kWinmmExports;
    if (name == L"shlwapi.dll") return kShlwapiExports;
    return kNoExports;
}

bool VerifyPatchedPe(const wchar_t* path, const std::vector<const char*>& requiredExports,
                     bool requireShimImport = true) {
    std::vector<BYTE> bytes;
    if (!ReadFileBuffer(path, bytes)) return false;
    auto* nt = GetPeHeaders(bytes);
    if (!nt) return false;
    bool metadataModified = false;
    if (!ClearModifiedImageHotpatchCompatibility(bytes, metadataModified) || metadataModified) return false;
    const auto* directories = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DataDirectory :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DataDirectory;
    DWORD offset = RvaToOffset(nt, nt->FileHeader.NumberOfSections, directories[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    bool foundImport = false;
    while (offset && IsBufferRange(bytes.size(), offset, sizeof(IMAGE_IMPORT_DESCRIPTOR))) {
        const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(bytes.data() + offset);
        if (!descriptor->Name && !descriptor->FirstThunk) break;
        std::string name;
        if (!ReadPeString(bytes, RvaToOffset(nt, nt->FileHeader.NumberOfSections, descriptor->Name), name)) return false;
        foundImport = foundImport || _stricmp(name.c_str(), "nanoserver_sta_shim.dll") == 0;
        offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
    }
    if (requireShimImport && !foundImport) return false;
    if (requiredExports.empty()) return true;
    DWORD exportOffset = RvaToOffset(nt, nt->FileHeader.NumberOfSections,
        directories[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    if (!exportOffset || !IsBufferRange(bytes.size(), exportOffset, sizeof(IMAGE_EXPORT_DIRECTORY))) return false;
    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes.data() + exportOffset);
    DWORD namesOffset = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfNames);
    DWORD ordinalsOffset = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfNameOrdinals);
    DWORD functionsOffset = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfFunctions);
    if (!namesOffset || !ordinalsOffset || !functionsOffset ||
        !IsBufferRange(bytes.size(), namesOffset, static_cast<size_t>(exports->NumberOfNames) * sizeof(DWORD)) ||
        !IsBufferRange(bytes.size(), ordinalsOffset, static_cast<size_t>(exports->NumberOfNames) * sizeof(WORD)) ||
        !IsBufferRange(bytes.size(), functionsOffset, static_cast<size_t>(exports->NumberOfFunctions) * sizeof(DWORD))) return false;
    const auto* names = reinterpret_cast<const DWORD*>(bytes.data() + namesOffset);
    const auto* ordinals = reinterpret_cast<const WORD*>(bytes.data() + ordinalsOffset);
    const auto* functions = reinterpret_cast<const DWORD*>(bytes.data() + functionsOffset);
    for (const char* required : requiredExports) {
        bool found = false;
        for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
            std::string name;
            if (!ReadPeString(bytes, RvaToOffset(nt, nt->FileHeader.NumberOfSections, names[i]), name)) return false;
            if (name == required) {
                if (ordinals[i] >= exports->NumberOfFunctions || functions[ordinals[i]] == 0) return false;
                DWORD fnRva = functions[ordinals[i]];
                const auto& expDir = directories[IMAGE_DIRECTORY_ENTRY_EXPORT];
                if (fnRva >= expDir.VirtualAddress && fnRva < expDir.VirtualAddress + expDir.Size) {
                    std::string target;
                    if (!ReadPeString(bytes, RvaToOffset(nt, nt->FileHeader.NumberOfSections, fnRva), target)) return false;
                    if (target != std::string("nanoserver_sta_shim.") + required) return false;
                }
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool PreflightPe(const std::wstring& path, WORD machine) {
    std::vector<BYTE> bytes;
    if (ReadFileBuffer(path.c_str(), bytes)) {
        auto* headers = GetPeHeaders(bytes);
        bool metadataModified = false;
        if (headers && headers->FileHeader.Machine == machine &&
            ClearModifiedImageHotpatchCompatibility(bytes, metadataModified)) return true;
        SetLastError(ERROR_BAD_EXE_FORMAT);
    }
    fwprintf(stderr, L"[ERR] Missing or invalid required PE: %s (err=%lu)\n", path.c_str(), GetLastError());
    return false;
}

bool VerifyOrdinalForwarders(const std::wstring& path, const std::vector<OrdinalExport>& expected) {
    std::vector<BYTE> bytes;
    if (!ReadFileBuffer(path.c_str(), bytes)) return false;
    auto* nt = GetPeHeaders(bytes);
    if (!nt) return false;
    const auto* directories = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DataDirectory :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DataDirectory;
    const auto& directory = directories[IMAGE_DIRECTORY_ENTRY_EXPORT];
    DWORD offset = RvaToOffset(nt, nt->FileHeader.NumberOfSections, directory.VirtualAddress);
    if (!offset || !IsBufferRange(bytes.size(), offset, sizeof(IMAGE_EXPORT_DIRECTORY))) return false;
    auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes.data() + offset);
    DWORD functions = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfFunctions);
    if (!functions || !IsBufferRange(bytes.size(), functions,
        static_cast<size_t>(exports->NumberOfFunctions) * sizeof(DWORD))) return false;
    for (const auto& entry : expected) {
        if (entry.Ordinal < exports->Base || entry.Ordinal - exports->Base >= exports->NumberOfFunctions) return false;
        DWORD target = reinterpret_cast<const DWORD*>(bytes.data() + functions)[entry.Ordinal - exports->Base];
        if (target < directory.VirtualAddress || target - directory.VirtualAddress >= directory.Size) return false;
        std::string actual;
        if (!ReadPeString(bytes, RvaToOffset(nt, nt->FileHeader.NumberOfSections, target), actual) ||
            actual != entry.Target) return false;
    }
    return true;
}

const std::vector<OrdinalExport>& CompanionExports(const wchar_t* name) {
    static const std::vector<OrdinalExport> olepro = {
        {248, "oleaut32.OleIconToCursor", "OleIconToCursor"},
        {249, "nanoserver_sta_shim.OleCreatePropertyFrameIndirect", "OleCreatePropertyFrameIndirect"},
        {250, "nanoserver_sta_shim.OleCreatePropertyFrame", "OleCreatePropertyFrame"},
        {251, "oleaut32.OleLoadPicture", "OleLoadPicture"},
        {252, "oleaut32.OleCreatePictureIndirect", "OleCreatePictureIndirect"},
        {253, "oleaut32.OleCreateFontIndirect", "OleCreateFontIndirect"},
        {254, "oleaut32.OleTranslateColor", "OleTranslateColor"},
        {255, "oleaut32.DllCanUnloadNow", "DllCanUnloadNow"},
        {256, "oleaut32.DllGetClassObject", "DllGetClassObject"},
        {257, "oleaut32.DllRegisterServer", "DllRegisterServer"},
        {258, "oleaut32.DllUnregisterServer", "DllUnregisterServer"}
    };
    static const std::vector<OrdinalExport> comsvcs = {
        {0, "nanoserver_sta_shim.DispManGetContext", "DispManGetContext"},
        {0, "nanoserver_sta_shim.NanoServerStaShim_DispenserDllCanUnloadNow", "DllCanUnloadNow"},
        {0, "nanoserver_sta_shim.NanoServerStaShim_DispenserDllGetClassObject", "DllGetClassObject"}
    };
    static const std::vector<OrdinalExport> mtxdm = {
        {0, "nanoserver_sta_shim.GetDispenserManager", "GetDispenserManager"}
    };
    return wcscmp(name, L"olepro32") == 0 ? olepro : wcscmp(name, L"comsvcs") == 0 ? comsvcs : mtxdm;
}

bool VerifyCompanionExports(const std::wstring& path, const std::vector<OrdinalExport>& expected) {
    std::vector<BYTE> bytes;
    if (!ReadFileBuffer(path.c_str(), bytes)) return false;
    auto* nt = GetPeHeaders(bytes);
    if (!nt) return false;
    const auto* directories = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        reinterpret_cast<IMAGE_NT_HEADERS32*>(nt)->OptionalHeader.DataDirectory :
        reinterpret_cast<IMAGE_NT_HEADERS64*>(nt)->OptionalHeader.DataDirectory;
    const auto& directory = directories[IMAGE_DIRECTORY_ENTRY_EXPORT];
    DWORD offset = RvaToOffset(nt, nt->FileHeader.NumberOfSections, directory.VirtualAddress);
    if (!offset || !IsBufferRange(bytes.size(), offset, sizeof(IMAGE_EXPORT_DIRECTORY))) return false;
    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes.data() + offset);
    if (exports->NumberOfNames != expected.size() || exports->NumberOfFunctions != expected.size()) return false;
    DWORD functions = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfFunctions);
    DWORD names = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfNames);
    DWORD ordinals = RvaToOffset(nt, nt->FileHeader.NumberOfSections, exports->AddressOfNameOrdinals);
    if (!functions || !names || !ordinals ||
        !IsBufferRange(bytes.size(), functions, expected.size() * sizeof(DWORD)) ||
        !IsBufferRange(bytes.size(), names, expected.size() * sizeof(DWORD)) ||
        !IsBufferRange(bytes.size(), ordinals, expected.size() * sizeof(WORD))) return false;
    for (const auto& entry : expected) {
        bool found = false;
        for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
            std::string name, target;
            if (!ReadPeString(bytes, RvaToOffset(nt, nt->FileHeader.NumberOfSections,
                reinterpret_cast<const DWORD*>(bytes.data() + names)[i]), name)) return false;
            if (name != entry.Name) continue;
            WORD ordinal = reinterpret_cast<const WORD*>(bytes.data() + ordinals)[i];
            if (ordinal >= exports->NumberOfFunctions ||
                (entry.Ordinal && exports->Base + ordinal != entry.Ordinal)) return false;
            DWORD rva = reinterpret_cast<const DWORD*>(bytes.data() + functions)[ordinal];
            if (rva < directory.VirtualAddress || rva - directory.VirtualAddress >= directory.Size ||
                !ReadPeString(bytes, RvaToOffset(nt, nt->FileHeader.NumberOfSections, rva), target) ||
                target != entry.Target) return false;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

bool PreflightArchitecture(const std::wstring& payload, const std::wstring& systemDirectory, WORD machine) {
    if (!PreflightPe(payload, machine)) return false;
    std::wstring directory = payload.substr(0, payload.find_last_of(L"\\/") + 1);
    for (const wchar_t* name : {L"olepro32", L"comsvcs", L"mtxdm"}) {
        std::wstring companion = directory + name + (machine == IMAGE_FILE_MACHINE_I386 ? L"_x86.dll" : L"_x64.dll");
        if (!PreflightPe(companion, machine) || !VerifyCompanionExports(companion, CompanionExports(name))) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            fwprintf(stderr, L"[ERR] Invalid companion forwarder ABI: %s\n", companion.c_str());
            return false;
        }
    }
    if (!PreflightPe(systemDirectory + L"\\forwarders\\kernel32.dll", machine)) return false;
    for (const wchar_t* name : {L"user32.dll", L"ole32.dll", L"gdi32.dll", L"comctl32.dll", L"shell32.dll", L"winmm.dll", L"shlwapi.dll"}) {
        if (!PreflightPe(systemDirectory + L"\\forwarders\\" + name, machine)) return false;
    }
    return true;
}

bool InstallArchitecture(const std::wstring& payload, const std::wstring& systemDirectory) {
    if (!CopyRequiredFile(payload, systemDirectory + L"\\nanoserver_sta_shim.dll") ||
        !CopyRequiredFile(payload, systemDirectory + L"\\forwarders\\nanoserver_sta_shim.dll")) return false;
    std::vector<BYTE> shimBytes;
    if (!ReadFileBuffer(payload.c_str(), shimBytes)) return false;
    auto* shimHeaders = GetPeHeaders(shimBytes);
    if (!shimHeaders) return false;
    const auto& shellOrdinals = ShlwapiOrdinals(shimHeaders->FileHeader.Machine);
    std::wstring directory = payload.substr(0, payload.find_last_of(L"\\/") + 1);
    for (const wchar_t* name : {L"olepro32", L"comsvcs", L"mtxdm"}) {
        std::wstring companion = directory + name + (shimHeaders->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 ?
            L"_x86.dll" : L"_x64.dll");
        if (!CopyRequiredFile(companion, systemDirectory + L"\\" + name + L".dll") ||
            !CopyRequiredFile(companion, systemDirectory + L"\\forwarders\\" + name + L".dll")) return false;
    }
    for (const wchar_t* name : {L"user32.dll", L"ole32.dll", L"gdi32.dll", L"comctl32.dll", L"shell32.dll", L"winmm.dll", L"shlwapi.dll"}) {
        std::wstring forwarder = systemDirectory + L"\\forwarders\\" + name;
        const auto& exports = RequiredExports(name);
        bool isShell = wcscmp(name, L"shlwapi.dll") == 0;
        const std::vector<OrdinalExport> noOrdinals;
        if (!PatchPeFileImportsAndExports(forwarder.c_str(), exports, isShell ? shellOrdinals : noOrdinals,
                wcscmp(name, L"ole32.dll") == 0) ||
            (isShell && !VerifyOrdinalForwarders(forwarder, shellOrdinals)) ||
            !VerifyPatchedPe(forwarder.c_str(), exports) ||
            !CopyRequiredFile(forwarder, systemDirectory + L"\\" + name) ||
            !VerifyPatchedPe((systemDirectory + L"\\" + name).c_str(), exports)) {
            fwprintf(stderr, L"[ERR] Required patch or verification failed: %s (err=%lu)\n", forwarder.c_str(), GetLastError());
            return false;
        }
    }
    // Patch only lazy compatibility forwarders. Do not inject a shim import into kernel32,
    // create a new root kernel32.dll, or alter KnownDLLs for this boot-critical DLL.
    std::wstring kernel32 = systemDirectory + L"\\forwarders\\kernel32.dll";
    const std::vector<const char*> compatibilityExports = {"_hread", "QueryActCtxW"};
    if (!PatchPeFileExports(kernel32.c_str(), compatibilityExports) ||
        !VerifyPatchedPe(kernel32.c_str(), compatibilityExports, false)) return false;
    wprintf(L"[OK] Installed and verified shim in %s\n", systemDirectory.c_str());
    return true;
}

// Keep both registry views reversible until the entire installation succeeds.
// Restore only values still equal to our writes, preserving concurrent edits.
class DispenserRegistration {
    struct Value {
        const wchar_t* Name = nullptr;
        bool Existed = false, Written = false;
        DWORD Type = 0;
        std::vector<BYTE> Original, Expected, Probe;
    };
    HKEY key_ = nullptr;
    bool committed_ = false;
    Value values_[2];

    bool Capture(Value& value, const wchar_t* name, const wchar_t* text) {
        value.Name = name;
        const BYTE* start = reinterpret_cast<const BYTE*>(text);
        value.Expected.assign(start, start + (wcslen(text) + 1) * sizeof(wchar_t));
        value.Probe.resize(value.Expected.size());
        DWORD size = 0;
        LSTATUS status = RegQueryValueExW(key_, name, nullptr, &value.Type, nullptr, &size);
        if (status == ERROR_FILE_NOT_FOUND) return true;
        // An unexpectedly large prior value fails before either value is written.
        if (status != ERROR_SUCCESS || size > 1024 * 1024) {
            SetLastError(status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status);
            return false;
        }
        value.Original.resize(size);
        BYTE empty = 0;
        status = RegQueryValueExW(key_, name, nullptr, &value.Type,
            size ? value.Original.data() : &empty, &size);
        if (status != ERROR_SUCCESS || size > value.Original.size()) {
            SetLastError(status == ERROR_SUCCESS ? ERROR_MORE_DATA : status);
            return false;
        }
        value.Original.resize(size);
        value.Existed = true;
        return true;
    }

    bool Write(Value& value) {
        LSTATUS status = RegSetValueExW(key_, value.Name, 0, REG_SZ, value.Expected.data(),
            static_cast<DWORD>(value.Expected.size()));
        value.Written = status == ERROR_SUCCESS;
        if (!value.Written) SetLastError(status);
        return value.Written;
    }

    void Restore(Value& value) noexcept {
        if (!value.Written) return;
        DWORD type = 0, size = static_cast<DWORD>(value.Probe.size());
        LSTATUS status = RegQueryValueExW(key_, value.Name, nullptr, &type, value.Probe.data(), &size);
        if (status != ERROR_SUCCESS || type != REG_SZ || size != value.Expected.size() ||
            memcmp(value.Probe.data(), value.Expected.data(), size)) return;
        status = value.Existed
            ? RegSetValueExW(key_, value.Name, 0, value.Type, value.Original.data(),
                static_cast<DWORD>(value.Original.size()))
            : RegDeleteValueW(key_, value.Name);
        if (status != ERROR_SUCCESS) {
            fwprintf(stderr, L"[ERR] Cannot restore an owned dispenser registry value (err=%ld).\n", status);
        }
    }

public:
    DispenserRegistration() = default;
    DispenserRegistration(const DispenserRegistration&) = delete;
    DispenserRegistration& operator=(const DispenserRegistration&) = delete;

    ~DispenserRegistration() {
        if (!key_) return;
        if (!committed_) {
            Restore(values_[1]);
            Restore(values_[0]);
        }
        RegCloseKey(key_);
    }

    bool Install(const std::wstring& systemDirectory, REGSAM view) {
        LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Classes\\CLSID\\{ECABB0C0-7F19-11D2-978E-0000F8757E2A}\\InprocServer32",
            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE | view, nullptr, &key_, nullptr);
        if (status != ERROR_SUCCESS) { SetLastError(status); return false; }
        std::wstring path = systemDirectory + L"\\comsvcs.dll";
        return Capture(values_[0], nullptr, path.c_str()) &&
            Capture(values_[1], L"ThreadingModel", L"Both") && Write(values_[0]) && Write(values_[1]);
    }

    void Commit() noexcept { committed_ = true; }
};

const wchar_t* const kOptionalWow64RuntimeDlls[] = {
    L"msvbvm60.dll",
    L"msvcr100.dll",
    L"msvcp100.dll",
    L"atl100.dll",
    L"mfc100.dll",
    L"mfc100u.dll",
    L"msimg32.dll",
    L"gdiplus.dll"
};

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (GetFileAttributesW(L"C:\\Windows\\System32\\win32u.dll") != INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"[ABORT] win32u.dll detected in System32. Refusing to run on a non-Nano-Server host OS.\n");
        return 1;
    }
    wchar_t executable[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length >= MAX_PATH) return 1;
    wchar_t* slash = wcsrchr(executable, L'\\');
    if (!slash) return 1;
    *slash = L'\0';
    std::wstring baseDirectory = executable;
    bool requireWow64 = false;
    bool hasBaseDirectory = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--require-wow64") == 0 || _wcsicmp(argv[i], L"/require-x86") == 0) {
            requireWow64 = true;
        } else if (!hasBaseDirectory && argv[i][0] != L'-' && argv[i][0] != L'/') {
            baseDirectory = argv[i];
            hasBaseDirectory = true;
        } else {
            fwprintf(stderr, L"Usage: install_sta_shim.exe [payload-directory] [--require-wow64 | /require-x86]\n");
            return 2;
        }
    }
    std::wstring shim64 = baseDirectory + L"\\nanoserver_sta_shim_x64.dll";
    std::wstring shim86 = baseDirectory + L"\\nanoserver_sta_shim_x86.dll";
    const std::wstring system32 = L"C:\\Windows\\System32";
    const std::wstring wow64 = L"C:\\Windows\\SysWOW64";
    DWORD wowAttributes = GetFileAttributesW((wow64 + L"\\forwarders").c_str());
    bool installWow64 = requireWow64 || (wowAttributes != INVALID_FILE_ATTRIBUTES && (wowAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    std::vector<const wchar_t*> presentWow64Runtimes;
    if (installWow64) {
        for (const wchar_t* runtimeDll : kOptionalWow64RuntimeDlls) {
            std::wstring candidate = baseDirectory + L"\\" + runtimeDll;
            DWORD attr = GetFileAttributesW(candidate.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                presentWow64Runtimes.push_back(runtimeDll);
            }
        }
    }
    // Complete all architecture preflights before touching a system file or registry value.
    if (!PreflightArchitecture(shim64, system32, IMAGE_FILE_MACHINE_AMD64) ||
        (installWow64 && !PreflightArchitecture(shim86, wow64, IMAGE_FILE_MACHINE_I386))) return 1;
    for (const wchar_t* runtimeDll : presentWow64Runtimes) {
        if (!PreflightPe(baseDirectory + L"\\" + runtimeDll, IMAGE_FILE_MACHINE_I386)) return 1;
    }
    if (!EnablePrivilege(L"SeBackupPrivilege") || !EnablePrivilege(L"SeRestorePrivilege")) {
        fwprintf(stderr, L"[ERR] Cannot enable installer privileges (err=%lu).\n", GetLastError());
        return 1;
    }
    HKEY key = nullptr;
    if (!OpenKnownDlls(&key)) {
        fwprintf(stderr, L"[ERR] Cannot open KnownDLLs before installation (err=%lu).\n", GetLastError());
        return 1;
    }
    if (RegCloseKey(key) != ERROR_SUCCESS) return 1;
    if (!InstallArchitecture(shim64, system32) ||
        (installWow64 && !InstallArchitecture(shim86, wow64))) return 1;
    DispenserRegistration registration64, registration32;
    if (!registration64.Install(system32, KEY_WOW64_64KEY) ||
        (installWow64 && !registration32.Install(wow64, KEY_WOW64_32KEY))) {
        fwprintf(stderr, L"[ERR] Cannot register the native dispenser (err=%lu).\n", GetLastError());
        return 1;
    }
    for (const wchar_t* runtimeDll : presentWow64Runtimes) {
        if (!CopyRequiredFile(baseDirectory + L"\\" + runtimeDll, wow64 + L"\\" + runtimeDll)) return 1;
    }
    if (!installWow64) wprintf(L"[OK] WOW64 not requested or present; installing x64 only.\n");
    if (!RemoveKnownDllEntries()) {
        fwprintf(stderr, L"[ERR] Cannot update KnownDLLs (err=%lu).\n", GetLastError());
        return 1;
    }
    registration64.Commit();
    registration32.Commit();
    return 0;
}
