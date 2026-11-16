// Nano exports some SxS APIs as nonfunctional stubs. Report an unavailable
// dynamic feature honestly instead of manufacturing an activation context.
struct AnsiProcedureName {
    USHORT Length;
    USHORT MaximumLength;
    const char* Buffer;
};
using NativeGetProcedureFn = LONG (NTAPI*)(PVOID, const AnsiProcedureName*, ULONG, PVOID*);
using NativeGetProcedureForCallerFn = LONG (NTAPI*)(PVOID, const AnsiProcedureName*, ULONG, PVOID*, ULONG, PVOID);

// Use SDK imports: these loader entries are not valid indirect CFG targets.
#pragma comment(lib, "ntdll.lib")
extern "C" __declspec(dllimport) LONG NTAPI LdrGetProcedureAddress(
    PVOID, const AnsiProcedureName*, ULONG, PVOID*);
extern "C" __declspec(dllimport) LONG NTAPI LdrGetProcedureAddressForCaller(
    PVOID, const AnsiProcedureName*, ULONG, PVOID*, ULONG, PVOID);
LONG NTAPI OriginalGetProcedure(PVOID module, const AnsiProcedureName* name, ULONG ordinal, PVOID* output) {
    return LdrGetProcedureAddress(module, name, ordinal, output);
}
LONG NTAPI OriginalGetProcedureForCaller(PVOID module, const AnsiProcedureName* name, ULONG ordinal,
                                         PVOID* output, ULONG flags, PVOID caller) {
    return LdrGetProcedureAddressForCaller(module, name, ordinal, output, flags, caller);
}

struct ProcedureApi {
    HMODULE Kernel32 = nullptr;
    HMODULE KernelBase = nullptr;
    NativeGetProcedureFn Get = nullptr;
    NativeGetProcedureForCallerFn GetForCaller = nullptr;
    bool SxsUnavailable = false;
};
ProcedureApi g_ProcedureApi;
INIT_ONCE g_ProcedureApiOnce = INIT_ONCE_STATIC_INIT;
bool g_ProcedureHooksAvailable = false;

BOOL CALLBACK InitializeProcedureApi(PINIT_ONCE, PVOID, PVOID*) {
    ProcedureApi api;
    api.Kernel32 = GetModuleHandleW(L"kernel32.dll");
    api.KernelBase = GetModuleHandleW(L"kernelbase.dll");
    api.Get = OriginalGetProcedure;
    api.GetForCaller = OriginalGetProcedureForCaller;
    if (!api.Kernel32 || !api.KernelBase || !api.Get || !api.GetForCaller) return FALSE;
    using FindFn = BOOL (WINAPI*)(DWORD, const GUID*, ULONG, LPCWSTR, PACTCTX_SECTION_KEYED_DATA);
    auto find = reinterpret_cast<FindFn>(GetProcAddress(api.Kernel32, "FindActCtxSectionStringW"));
    DWORD savedError = GetLastError();
    if (!find) api.SxsUnavailable = true;
    else {
        ACTCTX_SECTION_KEYED_DATA data{};
        data.cbSize = sizeof(data);
        SetLastError(ERROR_SUCCESS);
        BOOL found = find(0, nullptr, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
            L"__NanoStaShimMissingFeatureProbe__.dll", &data);
        DWORD error = GetLastError();
        api.SxsUnavailable = !found && (error == ERROR_INVALID_FUNCTION || error == ERROR_CALL_NOT_IMPLEMENTED);
    }
    SetLastError(savedError);
    g_ProcedureApi = api;
    return TRUE;
}

bool ReportUnavailableSxs(PVOID module, const AnsiProcedureName* name, PVOID* output) {
    constexpr char feature[] = "FindActCtxSectionStringW";
    if (!g_ProcedureApi.SxsUnavailable || !output || !name || !name->Buffer ||
        (module != g_ProcedureApi.Kernel32 && module != g_ProcedureApi.KernelBase) ||
        name->Length != sizeof(feature) - 1 || memcmp(name->Buffer, feature, sizeof(feature) - 1) ||
        IsOsShimDisabledByEnv()) return false;
    *output = nullptr;
    return true;
}

LONG NTAPI Shim_LdrGetProcedureAddress(PVOID module, const AnsiProcedureName* name, ULONG ordinal, PVOID* output) {
    if (ReportUnavailableSxs(module, name, output)) return static_cast<LONG>(0xC000007Au);
    return g_ProcedureApi.Get(module, name, ordinal, output);
}

LONG NTAPI Shim_LdrGetProcedureAddressForCaller(PVOID module, const AnsiProcedureName* name, ULONG ordinal,
                                               PVOID* output, ULONG flags, PVOID caller) {
    if (ReportUnavailableSxs(module, name, output)) return static_cast<LONG>(0xC000007Au);
    return g_ProcedureApi.GetForCaller(module, name, ordinal, output, flags, caller);
}

PVOID ResolveProviderShimFunction(const char* name) {
    if (PVOID atom = ResolveAtomShimFunction(name)) return atom;
    if (!g_ProcedureApi.Get || !g_ProcedureApi.GetForCaller) return nullptr;
    if (!strcmp(name, "LdrGetProcedureAddress")) return reinterpret_cast<PVOID>(Shim_LdrGetProcedureAddress);
    if (!strcmp(name, "LdrGetProcedureAddressForCaller")) return reinterpret_cast<PVOID>(Shim_LdrGetProcedureAddressForCaller);
    return nullptr;
}
