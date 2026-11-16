// Included inside the shim's implementation namespace.
// Layout is deliberately headless; persistence and COM ownership are real.

extern "C" BOOL WINAPI Shim_GetTextExtentExPointA(HDC, LPCSTR text, int count, int maximum,
                                      LPINT fit, LPINT advances, LPSIZE size) {
    if (!size || count < 0 || count > INT_MAX / 8 || (count && !text)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    int fitted = maximum > 0 ? (std::min)(count, maximum / 8) : 0;
    if (fit) *fit = fitted;
    if (advances) {
        int limit = fit ? fitted : count;
        for (int i = 0; i < limit; ++i) advances[i] = (i + 1) * 8;
    }
    size->cx = count * 8;
    size->cy = 16;
    return TRUE;
}

extern "C" LPWSTR WINAPI Shim_StrCpyW(LPWSTR destination, LPCWSTR source) {
    if (!destination || !source) return nullptr;
    LPWSTR output = destination;
    do { *output++ = *source; } while (*source++);
    return destination;
}

extern "C" LPWSTR WINAPI Shim_StrCatW(LPWSTR destination, LPCWSTR source) {
    if (!destination || !source) return nullptr;
    Shim_StrCpyW(destination + wcslen(destination), source);
    return destination;
}

extern "C" LPWSTR WINAPI Shim_StrCpyNW(LPWSTR destination, LPCWSTR source, int capacity) {
    if (!destination || !source || capacity <= 0) return destination;
    int i = 0;
    while (i < capacity - 1 && source[i]) { destination[i] = source[i]; ++i; }
    destination[i] = L'\0';
    return destination;
}

extern "C" LPWSTR WINAPI Shim_StrNCatW(LPWSTR destination, LPCWSTR source, int capacity) {
    if (!destination || !source || capacity <= 0) return destination;
    // Windows counts the appended segment, including its terminating NUL.
    // This is not StrCatBuffW's total destination-buffer capacity.
    Shim_StrCpyNW(destination + wcslen(destination), source, capacity);
    return destination;
}

thread_local unsigned g_OleInitializationDepth = 0;

extern "C" HRESULT WINAPI Shim_OleInitialize(LPVOID reserved) {
    HRESULT hr = CoInitializeEx(reserved, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        bool nested = g_OleInitializationDepth++ != 0;
        return nested ? S_FALSE : S_OK;
    }
    return hr;
}

extern "C" void WINAPI Shim_OleUninitialize() {
    if (g_OleInitializationDepth) {
        --g_OleInitializationDepth;
        CoUninitialize();
    }
}

extern "C" HRESULT WINAPI Shim_OleRun(LPUNKNOWN object) {
    if (!object) return E_INVALIDARG;
    IRunnableObject* runnable = nullptr;
    HRESULT hr = object->QueryInterface(__uuidof(IRunnableObject), reinterpret_cast<void**>(&runnable));
    if (hr == E_NOINTERFACE) return S_OK;
    if (FAILED(hr)) return hr;
    hr = runnable->Run(nullptr);
    runnable->Release();
    return hr;
}

extern "C" HRESULT WINAPI Shim_OleLockRunning(LPUNKNOWN object, BOOL lock, BOOL lastUnlockCloses) {
    if (!object) return E_INVALIDARG;
    IRunnableObject* runnable = nullptr;
    HRESULT hr = object->QueryInterface(__uuidof(IRunnableObject), reinterpret_cast<void**>(&runnable));
    if (hr == E_NOINTERFACE) return S_OK;
    if (FAILED(hr)) return hr;
    hr = runnable->LockRunning(lock, lastUnlockCloses);
    runnable->Release();
    return hr;
}

extern "C" HRESULT WINAPI Shim_OleSaveToStream(LPPERSISTSTREAM object, LPSTREAM stream) {
    if (!object || !stream) return E_INVALIDARG;
    CLSID clsid{};
    HRESULT hr = object->GetClassID(&clsid);
    if (FAILED(hr)) return hr;
    ULONG written = 0;
    hr = stream->Write(&clsid, sizeof(clsid), &written);
    if (FAILED(hr)) return hr;
    if (written != sizeof(clsid)) return STG_E_WRITEFAULT;
    return object->Save(stream, TRUE);
}

extern "C" HRESULT WINAPI Shim_OleLoadFromStream(LPSTREAM stream, REFIID iid, LPVOID* result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!stream) return E_INVALIDARG;
    CLSID clsid{};
    ULONG read = 0;
    HRESULT hr = stream->Read(&clsid, sizeof(clsid), &read);
    if (FAILED(hr)) return hr;
    if (read != sizeof(clsid)) return STG_E_READFAULT;
    IPersistStream* object = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IPersistStream),
                          reinterpret_cast<void**>(&object));
    if (FAILED(hr)) return hr;
    hr = object->Load(stream);
    if (SUCCEEDED(hr)) hr = object->QueryInterface(iid, result);
    object->Release();
    return hr;
}

extern "C" HRESULT WINAPI Shim_OleCreatePropertyFrame(HWND, UINT, UINT, LPCOLESTR, ULONG,
    LPUNKNOWN*, ULONG, LPCLSID, LCID, DWORD, LPVOID) { return E_NOTIMPL; }
extern "C" HRESULT WINAPI Shim_OleCreatePropertyFrameIndirect(LPVOID) { return E_NOTIMPL; }

void DeleteMediumGdiHandle(const char* name, HANDLE handle) {
    if (!handle) return;
    using DeleteFn = BOOL (WINAPI*)(HANDLE);
    auto remove = reinterpret_cast<DeleteFn>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), name));
    if (remove) remove(handle);
}

extern "C" void WINAPI Shim_ReleaseStgMedium(STGMEDIUM* medium) {
    if (!medium) return;
    STGMEDIUM owned = *medium;
    *medium = {};
    if (owned.pUnkForRelease && owned.tymed != TYMED_ISTREAM &&
        owned.tymed != TYMED_ISTORAGE && owned.tymed != TYMED_FILE) {
        owned.pUnkForRelease->Release();
        return;
    }
    switch (owned.tymed) {
    case TYMED_HGLOBAL: GlobalFree(owned.hGlobal); break;
    case TYMED_FILE:
        if (!owned.pUnkForRelease && owned.lpszFileName) DeleteFileW(owned.lpszFileName);
        CoTaskMemFree(owned.lpszFileName);
        break;
    case TYMED_ISTREAM: if (owned.pstm) owned.pstm->Release(); break;
    case TYMED_ISTORAGE: if (owned.pstg) owned.pstg->Release(); break;
    case TYMED_GDI: DeleteMediumGdiHandle("DeleteObject", owned.hBitmap); break;
    case TYMED_ENHMF: DeleteMediumGdiHandle("DeleteEnhMetaFile", owned.hEnhMetaFile); break;
    case TYMED_MFPICT: {
        auto picture = static_cast<METAFILEPICT*>(GlobalLock(owned.hMetaFilePict));
        if (picture) {
            DeleteMediumGdiHandle("DeleteMetaFile", picture->hMF);
            GlobalUnlock(owned.hMetaFilePict);
        }
        GlobalFree(owned.hMetaFilePict);
        break;
    }
    default: break;
    }
    if (owned.pUnkForRelease) owned.pUnkForRelease->Release();
}

class ScopedSrwExclusive {
public:
    explicit ScopedSrwExclusive(SRWLOCK& lock) : Lock(lock) { AcquireSRWLockExclusive(&Lock); }
    ~ScopedSrwExclusive() { ReleaseSRWLockExclusive(&Lock); }
private:
    SRWLOCK& Lock;
};

class ScopedComReference {
public:
    explicit ScopedComReference(IUnknown* object) : Object(object) { Object->AddRef(); }
    ~ScopedComReference() { Object->Release(); }
private:
    IUnknown* Object;
};

struct AdviseSubscription {
    FORMATETC Format{};
    DWORD Flags = 0;
    DWORD Cookie = 0;
    IAdviseSink* Sink = nullptr;
    volatile LONG Active = 1;

    ~AdviseSubscription() {
        if (Sink) Sink->Release();
        CoTaskMemFree(Format.ptd);
    }
};

HRESULT CopyAdviseFormat(const FORMATETC& source, FORMATETC& destination) {
    destination = source;
    destination.ptd = nullptr;
    if (!source.ptd) return S_OK;
    if (source.ptd->tdSize < sizeof(DVTARGETDEVICE)) return DV_E_DVTARGETDEVICE;
    auto copy = static_cast<DVTARGETDEVICE*>(CoTaskMemAlloc(source.ptd->tdSize));
    if (!copy) return E_OUTOFMEMORY;
    memcpy(copy, source.ptd, source.ptd->tdSize);
    destination.ptd = copy;
    return S_OK;
}

class CompatEnumStatData final : public IEnumSTATDATA {
public:
    explicit CompatEnumStatData(std::vector<std::shared_ptr<AdviseSubscription>> entries, size_t position = 0)
        : Entries(std::move(entries)), Position(position) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IEnumSTATDATA)) return E_NOINTERFACE;
        *output = static_cast<IEnumSTATDATA*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&References); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&References);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, STATDATA* output, ULONG* fetched) override {
        if (fetched) *fetched = 0;
        if (!output || (!fetched && count != 1)) return E_POINTER;
        ULONG copied = 0;
        while (copied < count && Position < Entries.size()) {
            const auto& entry = Entries[Position];
            output[copied] = {};
            HRESULT hr = CopyAdviseFormat(entry->Format, output[copied].formatetc);
            if (FAILED(hr)) {
                for (ULONG i = 0; i < copied; ++i) {
                    CoTaskMemFree(output[i].formatetc.ptd);
                    output[i].pAdvSink->Release();
                    output[i] = {};
                }
                Position -= copied;
                return hr;
            }
            output[copied].advf = entry->Flags;
            output[copied].dwConnection = entry->Cookie;
            output[copied].pAdvSink = entry->Sink;
            entry->Sink->AddRef();
            ++Position; ++copied;
        }
        if (fetched) *fetched = copied;
        return copied == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        size_t skipped = (std::min)(static_cast<size_t>(count), Entries.size() - Position);
        Position += skipped;
        return skipped == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { Position = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumSTATDATA** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        try { *output = new CompatEnumStatData(Entries, Position); return S_OK; }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
private:
    volatile LONG References = 1;
    std::vector<std::shared_ptr<AdviseSubscription>> Entries;
    size_t Position = 0;
};

class CompatDataAdviseHolder final : public IDataAdviseHolder {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDataAdviseHolder)) return E_NOINTERFACE;
        *output = static_cast<IDataAdviseHolder*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&References); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&References);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Advise(IDataObject* object, FORMATETC* format, DWORD flags,
                                      IAdviseSink* sink, DWORD* cookie) override {
        if (!cookie) return E_POINTER;
        *cookie = 0;
        if (!format || !sink) return E_INVALIDARG;
        if (flags & ~(ADVF_NODATA | ADVF_PRIMEFIRST | ADVF_ONLYONCE | ADVF_DATAONSTOP))
            return OLE_E_ADVISENOTSUPPORTED;
        ScopedComReference self(this);
        try {
            auto entry = std::make_shared<AdviseSubscription>();
            HRESULT hr = CopyAdviseFormat(*format, entry->Format);
            if (FAILED(hr)) return hr;
            sink->AddRef();
            entry->Sink = sink;
            entry->Flags = flags;
            {
                ScopedSrwExclusive lock(Lock);
                if (NextCookie == MAXDWORD) return E_OUTOFMEMORY;
                entry->Cookie = ++NextCookie;
                Entries.push_back(entry);
            }
            *cookie = entry->Cookie;
            if (flags & ADVF_PRIMEFIRST) {
                Notify(entry, object);
                if (!InterlockedCompareExchange(&entry->Active, 1, 1)) *cookie = 0;
            }
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) override {
        if (!cookie) return S_OK;
        std::shared_ptr<AdviseSubscription> removed;
        {
            ScopedSrwExclusive lock(Lock);
            for (auto it = Entries.begin(); it != Entries.end(); ++it) {
                if ((*it)->Cookie == cookie) {
                    removed = *it;
                    Entries.erase(it);
                    break;
                }
            }
        }
        if (!removed) return OLE_E_NOCONNECTION;
        InterlockedExchange(&removed->Active, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumAdvise(IEnumSTATDATA** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        try {
            auto entries = Snapshot();
            *output = new CompatEnumStatData(std::move(entries));
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE SendOnDataChange(IDataObject* object, DWORD, DWORD flags) override {
        if (flags & ~ADVF_DATAONSTOP) return E_INVALIDARG;
        ScopedComReference self(this);
        try {
            auto entries = Snapshot();
            for (const auto& entry : entries) {
                bool onStop = (flags & ADVF_DATAONSTOP) != 0;
                if (onStop && !(entry->Flags & ADVF_DATAONSTOP)) continue;
                Notify(entry, object, onStop);
            }
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
private:
    std::vector<std::shared_ptr<AdviseSubscription>> Snapshot() {
        ScopedSrwExclusive lock(Lock);
        return Entries;
    }
    void Notify(const std::shared_ptr<AdviseSubscription>& entry, IDataObject* object, bool onStop = false) {
        if (!InterlockedCompareExchange(&entry->Active, 1, 1)) return;
        STGMEDIUM medium{};
        bool noData = (entry->Flags & ADVF_NODATA) != 0 && !onStop;
        if (!noData && (!object || FAILED(object->GetData(&entry->Format, &medium)))) return;
        bool notify = true;
        if (entry->Flags & ADVF_ONLYONCE) {
            notify = InterlockedCompareExchange(&entry->Active, 0, 1) == 1;
            if (notify) Unadvise(entry->Cookie);
        }
        if (notify) entry->Sink->OnDataChange(&entry->Format, noData ? nullptr : &medium);
        if (!noData) Shim_ReleaseStgMedium(&medium);
    }
    volatile LONG References = 1;
    SRWLOCK Lock = SRWLOCK_INIT;
    DWORD NextCookie = 0;
    std::vector<std::shared_ptr<AdviseSubscription>> Entries;
};

extern "C" HRESULT WINAPI Shim_CreateDataAdviseHolder(IDataAdviseHolder** output) {
    if (!output) return E_POINTER;
    *output = new (std::nothrow) CompatDataAdviseHolder();
    return *output ? S_OK : E_OUTOFMEMORY;
}

