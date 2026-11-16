// Route the atom provider's native imports, not imports in arbitrary clients.
// Its imports are bound before the shim runs, including when clients have yet
// to run DllMain. The provider is fixed for this process; namespaces never mix.
using AtomStatus = LONG;
using NtAddAtomExFn = AtomStatus (NTAPI*)(PWSTR, ULONG, ATOM*, ULONG);
using NtFindAtomFn = AtomStatus (NTAPI*)(PWSTR, ULONG, ATOM*);
using NtDeleteAtomFn = AtomStatus (NTAPI*)(ATOM);
using NtQueryAtomFn = AtomStatus (NTAPI*)(ATOM, ULONG, PVOID, ULONG, PULONG);
using RtlCreateAtomTableFn = AtomStatus (NTAPI*)(ULONG, PVOID*);
using RtlAddAtomFn = AtomStatus (NTAPI*)(PVOID, PWSTR, ATOM*);
using RtlDeleteAtomFn = AtomStatus (NTAPI*)(PVOID, ATOM);
using RtlQueryAtomFn = AtomStatus (NTAPI*)(PVOID, ATOM, PULONG, PULONG, PWSTR, PULONG);

struct AtomApi {
    NtAddAtomExFn Add = nullptr;
    NtFindAtomFn Find = nullptr;
    NtDeleteAtomFn Delete = nullptr;
    NtQueryAtomFn Query = nullptr;
    RtlAddAtomFn LocalAdd = nullptr;
    RtlAddAtomFn LocalFind = nullptr;
    RtlDeleteAtomFn LocalDelete = nullptr;
    RtlQueryAtomFn LocalQuery = nullptr;
    PVOID Table = nullptr;
};
AtomApi g_AtomApi;
INIT_ONCE g_AtomApiOnce = INIT_ONCE_STATIC_INIT;
// 0 = undecided, 1 = original OS namespace, 2 = private fallback namespace.
volatile LONG g_AtomProvider = 0;
bool AtomFallbackStatus(AtomStatus status);

BOOL CALLBACK InitializeAtomApi(PINIT_ONCE, PVOID, PVOID*) {
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    if (!module) return FALSE;
    AtomApi api;
    api.Add = reinterpret_cast<NtAddAtomExFn>(GetProcAddress(module, "NtAddAtomEx"));
    api.Find = reinterpret_cast<NtFindAtomFn>(GetProcAddress(module, "NtFindAtom"));
    api.Delete = reinterpret_cast<NtDeleteAtomFn>(GetProcAddress(module, "NtDeleteAtom"));
    api.Query = reinterpret_cast<NtQueryAtomFn>(GetProcAddress(module, "NtQueryInformationAtom"));
    api.LocalAdd = reinterpret_cast<RtlAddAtomFn>(GetProcAddress(module, "RtlAddAtomToAtomTable"));
    api.LocalFind = reinterpret_cast<RtlAddAtomFn>(GetProcAddress(module, "RtlLookupAtomInAtomTable"));
    api.LocalDelete = reinterpret_cast<RtlDeleteAtomFn>(GetProcAddress(module, "RtlDeleteAtomFromAtomTable"));
    api.LocalQuery = reinterpret_cast<RtlQueryAtomFn>(GetProcAddress(module, "RtlQueryAtomInAtomTable"));
    auto create = reinterpret_cast<RtlCreateAtomTableFn>(GetProcAddress(module, "RtlCreateAtomTable"));
    if (!api.Add || !api.Find || !api.Delete || !api.Query || !api.LocalAdd ||
        !api.LocalFind || !api.LocalDelete || !api.LocalQuery || !create) return FALSE;
    if (create(37, &api.Table) < 0) return FALSE;
    wchar_t probeName[64]{};
    _snwprintf_s(probeName, _countof(probeName), _TRUNCATE, L"NanoStaAtomProbe_%lu", GetCurrentProcessId());
    ATOM probe = 0;
    AtomStatus status = api.Add(probeName, static_cast<ULONG>(wcslen(probeName) * sizeof(wchar_t)), &probe, 0);
    if (status >= 0) api.Delete(probe);
    // The shim is process-resident. Keep the table alive across disable/restore;
    // destroying it while another thread owns an atom would invalidate handles.
    g_AtomApi = api;
    // Serialize provider selection before any caller receives an atom.
    InterlockedExchange(&g_AtomProvider, AtomFallbackStatus(status) ? 2 : 1);
    return TRUE;
}

bool EnsureAtomApi() { return InitOnceExecuteOnce(&g_AtomApiOnce, InitializeAtomApi, nullptr, nullptr) != FALSE; }
bool AtomFallbackStatus(AtomStatus status) {
    return status == static_cast<LONG>(0xC0000022u) || // access denied
           status == static_cast<LONG>(0xC0000008u) || // no usable window station
           status == static_cast<LONG>(0xC0000002u);   // not implemented
}

AtomStatus CopyNativeAtomName(PWSTR name, ULONG bytes, wchar_t (&copy)[256]) {
    if (!name || !bytes || (bytes & 1) || bytes > 255 * sizeof(wchar_t))
        return static_cast<LONG>(0xC000000Du);
    memcpy(copy, name, bytes);
    copy[bytes / sizeof(wchar_t)] = L'\0';
    return 0;
}

AtomStatus NTAPI Shim_NtAddAtomEx(PWSTR name, ULONG bytes, ATOM* atom, ULONG flags) {
    if (!EnsureAtomApi()) return static_cast<LONG>(0xC0000002u);
    if (InterlockedCompareExchange(&g_AtomProvider, 0, 0) != 2)
        return g_AtomApi.Add(name, bytes, atom, flags);
    if (!atom || flags) return static_cast<LONG>(0xC000000Du);
    wchar_t copy[256];
    AtomStatus status = CopyNativeAtomName(name, bytes, copy);
    return status < 0 ? status : g_AtomApi.LocalAdd(g_AtomApi.Table, copy, atom);
}

AtomStatus NTAPI Shim_NtFindAtom(PWSTR name, ULONG bytes, ATOM* atom) {
    if (!EnsureAtomApi()) return static_cast<LONG>(0xC0000002u);
    if (InterlockedCompareExchange(&g_AtomProvider, 0, 0) != 2)
        return g_AtomApi.Find(name, bytes, atom);
    if (!atom) return static_cast<LONG>(0xC000000Du);
    wchar_t copy[256];
    AtomStatus status = CopyNativeAtomName(name, bytes, copy);
    return status < 0 ? status : g_AtomApi.LocalFind(g_AtomApi.Table, copy, atom);
}

AtomStatus NTAPI Shim_NtDeleteAtom(ATOM atom) {
    if (!EnsureAtomApi()) return static_cast<LONG>(0xC0000002u);
    if (InterlockedCompareExchange(&g_AtomProvider, 0, 0) != 2) return g_AtomApi.Delete(atom);
    return g_AtomApi.LocalDelete(g_AtomApi.Table, atom);
}

AtomStatus NTAPI Shim_NtQueryInformationAtom(ATOM atom, ULONG informationClass, PVOID information,
                                           ULONG bytes, PULONG returned) {
    if (!EnsureAtomApi()) return static_cast<LONG>(0xC0000002u);
    if (InterlockedCompareExchange(&g_AtomProvider, 0, 0) != 2)
        return g_AtomApi.Query(atom, informationClass, information, bytes, returned);
    if (informationClass != 0) return static_cast<LONG>(0xC0000003u);
    struct BasicInformation { USHORT References; USHORT Flags; USHORT NameBytes; WCHAR Name[1]; };
    const ULONG headerBytes = FIELD_OFFSET(BasicInformation, Name);
    if (!information || bytes < headerBytes) {
        if (returned) *returned = 0;
        return static_cast<LONG>(0xC0000004u);
    }
    if (bytes <= headerBytes + sizeof(wchar_t)) {
        if (returned) *returned = headerBytes;
        return static_cast<LONG>(0xC0000023u);
    }
    wchar_t name[256]{};
    ULONG nameBytes = sizeof(name), references = 0, flags = 0;
    AtomStatus status = g_AtomApi.LocalQuery(g_AtomApi.Table, atom, &references, &flags, name, &nameBytes);
    if (status < 0) return status;
    // NtQueryInformationAtom truncates successfully when at least one name
    // character and a NUL fit. Match the Server Core control byte for byte.
    ULONG capacity = (bytes - headerBytes - static_cast<ULONG>(sizeof(wchar_t))) & ~1ul;
    ULONG copied = (std::min)(nameBytes, capacity);
    ULONG required = headerBytes + copied + sizeof(wchar_t);
    if (returned) *returned = required;
    auto* result = static_cast<BasicInformation*>(information);
    result->References = static_cast<USHORT>((std::min)(references, 0xFFFFul));
    result->Flags = static_cast<USHORT>(flags);
    result->NameBytes = static_cast<USHORT>(copied);
    memcpy(result->Name, name, copied);
    result->Name[copied / sizeof(wchar_t)] = L'\0';
    return 0;
}

PVOID ResolveAtomShimFunction(const char* name) {
    if (!strcmp(name, "NtAddAtomEx")) return reinterpret_cast<PVOID>(Shim_NtAddAtomEx);
    if (!strcmp(name, "NtFindAtom")) return reinterpret_cast<PVOID>(Shim_NtFindAtom);
    if (!strcmp(name, "NtDeleteAtom")) return reinterpret_cast<PVOID>(Shim_NtDeleteAtom);
    if (!strcmp(name, "NtQueryInformationAtom")) return reinterpret_cast<PVOID>(Shim_NtQueryInformationAtom);
    return nullptr;
}
