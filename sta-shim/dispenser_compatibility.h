// A native, nontransactional, zero-retention dispenser. It does not implement
// COM+ transactions or cross-object pooling. Driver callbacks never run under
// the inventory lock, and in-flight operations retain the driver after Close.
constexpr GUID kDispenserManagerClass =
    {0xecabb0c0, 0x7f19, 0x11d2, {0x97, 0x8e, 0x00, 0x00, 0xf8, 0x75, 0x7e, 0x2a}};

struct DispenserDriverOwner {
    explicit DispenserDriverOwner(IDispenserDriver* driver) : Driver(driver) { Driver->AddRef(); }
    ~DispenserDriverOwner() { Driver->Release(); }
    IDispenserDriver* Driver;
};

struct DispenserResource {
    std::shared_ptr<DispenserDriverOwner> Owner;
    RESID Id = 0;
    std::wstring Name;
    bool IsString = false;
    bool OwnsResource = false;
    volatile LONG Destroyed = 0;
    volatile LONG Busy = 0;
    HRESULT Destroy() {
        if (!OwnsResource || InterlockedExchange(&Destroyed, 1)) return S_OK;
        return IsString ? Owner->Driver->DestroyResourceS(Name.c_str()) : Owner->Driver->DestroyResource(Id);
    }
    ~DispenserResource() { Destroy(); }
};

class CompatResourceHolder final : public IHolder {
public:
    explicit CompatResourceHolder(IDispenserDriver* driver)
        : Owner(std::make_shared<DispenserDriverOwner>(driver)) {}
    ~CompatResourceHolder() { CloseInventory(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IHolder)) return E_NOINTERFACE;
        *output = static_cast<IHolder*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&References); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&References);
        if (!count) { InterlockedExchange(&Finalizing, 1); delete this; }
        return count;
    }
    HRESULT STDMETHODCALLTYPE AllocResource(RESTYPID type, RESID* output) override {
        if (!output) return E_POINTER;
        *output = 0;
        if (Finalizing) return CO_E_OBJNOTCONNECTED;
        ScopedComReference self(this);
        try {
            auto resource = std::make_shared<DispenserResource>();
            {
                ScopedSrwExclusive lock(Lock);
                if (Closed) return CO_E_OBJNOTCONNECTED;
                resource->Owner = Owner;
            }
            TIMEINSECS lifetime = 0;
            HRESULT hr = resource->Owner->Driver->CreateResource(type, &resource->Id, &lifetime);
            if (FAILED(hr)) return hr;
            resource->OwnsResource = true;
            if (!resource->Id) return E_FAIL;
            {
                ScopedSrwExclusive lock(Lock);
                if (Closed) return CO_E_OBJNOTCONNECTED;
                for (const auto& entry : Inventory)
                    if (!entry->IsString && entry->Id == resource->Id) {
                        // A broken driver returned an already-owned handle.
                        resource->OwnsResource = false;
                        return E_UNEXPECTED;
                    }
                Inventory.push_back(resource);
            }
            *output = resource->Id;
            return hr;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE FreeResource(RESID id) override {
        if (Finalizing) return CO_E_OBJNOTCONNECTED;
        ScopedComReference self(this);
        std::shared_ptr<DispenserResource> resource;
        {
            ScopedSrwExclusive lock(Lock);
            for (const auto& entry : Inventory)
                if (!entry->IsString && entry->Id == id) { resource = entry; break; }
            if (!resource) return E_INVALIDARG;
            if (InterlockedCompareExchange(&resource->Busy, 1, 0)) return E_PENDING;
        }
        HRESULT hr = resource->Owner->Driver->ResetResource(id);
        if (SUCCEEDED(hr)) {
            {
                ScopedSrwExclusive lock(Lock);
                Inventory.erase(std::remove(Inventory.begin(), Inventory.end(), resource), Inventory.end());
            }
            hr = resource->Destroy();
        }
        InterlockedExchange(&resource->Busy, 0);
        // If Close ran inside ResetResource, the local reference performs the
        // deferred destruction, including when ResetResource failed.
        return hr;
    }
    HRESULT STDMETHODCALLTYPE TrackResource(RESID id) override { return Track(id, nullptr); }
    HRESULT STDMETHODCALLTYPE TrackResourceS(constSRESID name) override { return Track(0, name); }
    HRESULT STDMETHODCALLTYPE UntrackResource(RESID id, BOOL destroy) override { return Untrack(id, nullptr, destroy); }
    HRESULT STDMETHODCALLTYPE UntrackResourceS(constSRESID name, BOOL destroy) override { return Untrack(0, name, destroy); }
    HRESULT STDMETHODCALLTYPE RequestDestroyResource(RESID id) override { return Untrack(id, nullptr, TRUE); }
    HRESULT STDMETHODCALLTYPE Close() override {
        if (Finalizing) return S_OK;
        ScopedComReference self(this);
        return CloseInventory();
    }
private:
    HRESULT Track(RESID id, LPCWSTR name) {
        if ((!name && !id) || (name && !*name)) return E_INVALIDARG;
        if (Finalizing) return CO_E_OBJNOTCONNECTED;
        ScopedComReference self(this);
        try {
            auto resource = std::make_shared<DispenserResource>();
            resource->Id = id;
            resource->IsString = name != nullptr;
            if (name) resource->Name = name;
            ScopedSrwExclusive lock(Lock);
            if (Closed) return CO_E_OBJNOTCONNECTED;
            for (const auto& entry : Inventory)
                if (Matches(*entry, id, name)) return E_INVALIDARG;
            resource->Owner = Owner;
            Inventory.push_back(resource);
            resource->OwnsResource = true;
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    static bool Matches(const DispenserResource& resource, RESID id, LPCWSTR name) {
        return name ? resource.IsString && resource.Name == name : !resource.IsString && resource.Id == id;
    }
    HRESULT Untrack(RESID id, LPCWSTR name, BOOL destroy) {
        if (Finalizing) return CO_E_OBJNOTCONNECTED;
        ScopedComReference self(this);
        std::shared_ptr<DispenserResource> resource;
        {
            ScopedSrwExclusive lock(Lock);
            auto found = std::find_if(Inventory.begin(), Inventory.end(),
                [=](const auto& entry) { return Matches(*entry, id, name); });
            if (found == Inventory.end()) return E_INVALIDARG;
            resource = *found;
            if (InterlockedCompareExchange(&resource->Busy, 1, 0)) return E_PENDING;
            Inventory.erase(found);
        }
        if (!destroy) resource->OwnsResource = false;
        return destroy ? resource->Destroy() : S_OK;
    }
    HRESULT CloseInventory() {
        std::vector<std::shared_ptr<DispenserResource>> removed;
        std::shared_ptr<DispenserDriverOwner> driver;
        {
            ScopedSrwExclusive lock(Lock);
            Closed = true;
            removed.swap(Inventory);
            driver.swap(Owner);
        }
        HRESULT result = S_OK;
        for (const auto& resource : removed) {
            if (InterlockedCompareExchange(&resource->Busy, 1, 0)) continue;
            HRESULT hr = resource->Destroy();
            if (FAILED(hr) && SUCCEEDED(result)) result = hr;
        }
        return result;
    }
    volatile LONG References = 1;
    volatile LONG Finalizing = 0;
    SRWLOCK Lock = SRWLOCK_INIT;
    bool Closed = false;
    std::shared_ptr<DispenserDriverOwner> Owner;
    std::vector<std::shared_ptr<DispenserResource>> Inventory;
};

extern "C" HRESULT WINAPI Shim_DispManGetContext(INSTID* instance, TRANSID* transaction) {
    if (instance) *instance = 0;
    if (transaction) *transaction = 0;
    return instance && transaction ? S_OK : E_POINTER;
}

class CompatDispenserManager final : public IDispenserManager {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDispenserManager)) return E_NOINTERFACE;
        *output = static_cast<IDispenserManager*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&References); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&References);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE RegisterDispenser(IDispenserDriver* driver, LPCOLESTR name, IHolder** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!driver || !name || !*name) return E_INVALIDARG;
        try { *output = new CompatResourceHolder(driver); return S_OK; }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE GetContext(INSTID* instance, TRANSID* transaction) override {
        return Shim_DispManGetContext(instance, transaction);
    }
private:
    volatile LONG References = 1;
};

extern "C" HRESULT __cdecl Shim_GetDispenserManager(IDispenserManager** output) {
    if (!output) return E_POINTER;
    *output = new (std::nothrow) CompatDispenserManager();
    return *output ? S_OK : E_OUTOFMEMORY;
}

class CompatDispenserFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IClassFactory)) return E_NOINTERFACE;
        *output = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&References); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&References);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        IDispenserManager* manager = nullptr;
        HRESULT hr = Shim_GetDispenserManager(&manager);
        if (FAILED(hr)) return hr;
        hr = manager->QueryInterface(iid, output);
        manager->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
private:
    volatile LONG References = 1;
};

extern "C" HRESULT WINAPI NanoServerStaShim_DispenserDllGetClassObject(REFCLSID clsid, REFIID iid, void** output) {
    if (!output) return E_POINTER;
    *output = nullptr;
    if (clsid != kDispenserManagerClass) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new (std::nothrow) CompatDispenserFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, output);
    factory->Release();
    return hr;
}

extern "C" HRESULT WINAPI NanoServerStaShim_DispenserDllCanUnloadNow() {
    // Both the shim and its callback code are intentionally process-resident.
    return S_FALSE;
}
