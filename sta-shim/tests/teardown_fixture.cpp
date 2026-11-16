// Native-only lifetime discriminator. The manager is an ordinary mtxdm import.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ComSvcs.h>
extern "C" __declspec(dllimport) HRESULT __cdecl GetDispenserManager(IDispenserManager**);

namespace {
volatile LONG successfulThreads = 0, failures = 0, probeThreads = 0;
HANDLE processCompleted = nullptr;
class Driver final : public IDispenserDriver {
public:
    volatile LONG References = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDispenserDriver)) return E_NOINTERFACE;
        *output = static_cast<IDispenserDriver*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&References); }
    ULONG STDMETHODCALLTYPE Release() override { return InterlockedDecrement(&References); }
    HRESULT STDMETHODCALLTYPE CreateResource(RESTYPID, RESID* output, TIMEINSECS* seconds) override {
        *output = 7; *seconds = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RateResource(RESTYPID, RESID, BOOL, RESOURCERATING* output) override {
        *output = 100; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnlistResource(RESID, TRANSID) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE ResetResource(RESID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DestroyResource(RESID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DestroyResourceS(constSRESID) override { return S_OK; }
} driver;
IHolder* processHolder = nullptr;
__declspec(thread) IHolder* threadHolder = nullptr;

bool CreateHolder(IHolder** output) {
    IDispenserManager* manager = nullptr;
    HRESULT hr = GetDispenserManager(&manager);
    if (FAILED(hr) || !manager) return false;
    hr = manager->RegisterDispenser(&driver, L"NativeTeardownFixture", output);
    manager->Release();
    return SUCCEEDED(hr) && *output && (*output)->TrackResource(7) == S_OK;
}
bool CloseHolder(IHolder*& holder) {
    if (!holder) return true;
    bool success = holder->Close() == S_OK && holder->Close() == S_OK;
    success = holder->Release() == 0 && success;
    holder = nullptr;
    return success;
}
}

extern "C" BOOL __cdecl TeardownFixtureConfigure(HANDLE event, BOOL enableThreads) {
    processCompleted = event;
    InterlockedExchange(&probeThreads, enableThreads ? 1 : 0);
    return processHolder != nullptr;
}
extern "C" DWORD __cdecl TeardownFixtureStatus() {
    return static_cast<DWORD>(successfulThreads) | (static_cast<DWORD>(failures) << 16);
}
BOOL WINAPI DllMain(HMODULE, DWORD reason, PVOID) {
    if (reason == DLL_PROCESS_ATTACH) return CreateHolder(&processHolder);
    if (reason == DLL_THREAD_ATTACH && InterlockedCompareExchange(&probeThreads, 0, 0)) {
        if (!CreateHolder(&threadHolder)) InterlockedIncrement(&failures);
    } else if (reason == DLL_THREAD_DETACH && threadHolder) {
        if (CloseHolder(threadHolder)) InterlockedIncrement(&successfulThreads);
        else InterlockedIncrement(&failures);
    } else if (reason == DLL_PROCESS_DETACH) {
        bool success = CloseHolder(processHolder) && driver.References == 1 && !failures;
        if (success && processCompleted) SetEvent(processCompleted);
    }
    return TRUE;
}
