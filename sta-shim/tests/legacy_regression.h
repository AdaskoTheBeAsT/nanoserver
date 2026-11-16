// Real public imports. No shim implementation is included in these tests.
class TestPersist final : public IPersistStream {
public:
    static constexpr GUID ClassId = {0xb12ac311, 0x5d2b, 0x48ca, {0x9c,0x31,0x49,0x12,0xa7,0xa8,0x22,0x01}};
    ULONG References = 1;
    DWORD Value = 0x1234abcd;
    HRESULT SaveResult = S_OK;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IPersist) && iid != __uuidof(IPersistStream))
            return E_NOINTERFACE;
        *result = static_cast<IPersistStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++References; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG value = --References; if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* result) override { if (!result) return E_POINTER; *result = ClassId; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsDirty() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Load(IStream* stream) override {
        ULONG read = 0;
        HRESULT hr = stream->Read(&Value, sizeof(Value), &read);
        return FAILED(hr) ? hr : read == sizeof(Value) ? S_OK : STG_E_READFAULT;
    }
    HRESULT STDMETHODCALLTYPE Save(IStream* stream, BOOL) override {
        if (FAILED(SaveResult)) return SaveResult;
        ULONG written = 0;
        HRESULT hr = stream->Write(&Value, sizeof(Value), &written);
        return FAILED(hr) ? hr : written == sizeof(Value) ? S_OK : STG_E_WRITEFAULT;
    }
    HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER* size) override { size->QuadPart = sizeof(Value); return S_OK; }
};

class TestPersistFactory final : public IClassFactory {
public:
    ULONG References = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IClassFactory)) return E_NOINTERFACE;
        *result = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++References; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG value = --References; if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto object = new TestPersist();
        HRESULT hr = object->QueryInterface(iid, result);
        object->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

class TestStream final : public IStream {
public:
    std::vector<BYTE> Bytes;
    size_t Position = 0;
    bool ShortWrite = false;
    HRESULT ReadResult = S_OK;
    ULONG Releases = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** result) override { *result = nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { ++Releases; return 1; }
    HRESULT STDMETHODCALLTYPE Read(void* output, ULONG count, ULONG* read) override {
        if (FAILED(ReadResult)) return ReadResult;
        ULONG copied = static_cast<ULONG>((std::min)(static_cast<size_t>(count), Bytes.size() - Position));
        memcpy(output, Bytes.data() + Position, copied); Position += copied;
        if (read) *read = copied;
        return copied == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* input, ULONG count, ULONG* written) override {
        ULONG copied = ShortWrite && count ? count - 1 : count;
        Bytes.resize(Position + copied);
        memcpy(Bytes.data() + Position, input, copied); Position += copied;
        if (written) *written = copied;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
};

class TestAdviseSink final : public IAdviseSink {
public:
    ULONG References = 1;
    unsigned Calls = 0;
    unsigned DataCalls = 0;
    IDataAdviseHolder* Holder = nullptr;
    DWORD* Cookie = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IAdviseSink)) return E_NOINTERFACE;
        *result = static_cast<IAdviseSink*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++References; }
    ULONG STDMETHODCALLTYPE Release() override { return --References; }
    void STDMETHODCALLTYPE OnDataChange(FORMATETC*, STGMEDIUM* medium) override {
        ++Calls;
        if (medium && medium->tymed != TYMED_NULL) ++DataCalls;
        if (Holder && Cookie) Holder->Unadvise(*Cookie);
    }
    void STDMETHODCALLTYPE OnViewChange(DWORD, LONG) override {}
    void STDMETHODCALLTYPE OnRename(IMoniker*) override {}
    void STDMETHODCALLTYPE OnSave() override {}
    void STDMETHODCALLTYPE OnClose() override {}
};

void TestOleInitialization() {
    std::thread thread([] {
        Check("ole_first_initialization", OleInitialize(nullptr) == S_OK);
        Check("ole_nested_initialization", OleInitialize(nullptr) == S_FALSE);
        OleUninitialize();
        APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
        Check("ole_one_balance_retains_apartment", SUCCEEDED(CoGetApartmentType(&type, &qualifier)));
        OleUninitialize();
        Check("ole_all_balanced", CoGetApartmentType(&type, &qualifier) == CO_E_NOTINITIALIZED);
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Check("ole_mta_setup", SUCCEEDED(hr));
        Check("ole_preserves_changed_mode", OleInitialize(nullptr) == RPC_E_CHANGED_MODE);
        OleUninitialize();
        Check("ole_failed_call_does_not_uninitialize_mta",
            SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && type == APTTYPE_MTA);
        if (SUCCEEDED(hr)) CoUninitialize();
    });
    thread.join();
}

void TestOlePersistence() {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Require(SUCCEEDED(init), "initialize persistence apartment");
    auto object = new TestPersist();
    TestStream stream;
    Check("ole_save_class_and_state", SUCCEEDED(OleSaveToStream(object, &stream)) &&
        stream.Bytes.size() == sizeof(CLSID) + sizeof(DWORD) &&
        memcmp(stream.Bytes.data(), &TestPersist::ClassId, sizeof(CLSID)) == 0);
    stream.ShortWrite = true; stream.Position = 0;
    Check("ole_save_short_write", FAILED(OleSaveToStream(object, &stream)));
    stream.ShortWrite = false; stream.Position = 0;
    object->SaveResult = E_ACCESSDENIED;
    Check("ole_save_error_preserved", OleSaveToStream(object, &stream) == E_ACCESSDENIED);
    object->SaveResult = S_OK;
    stream.Position = 0; OleSaveToStream(object, &stream); stream.Position = 0;
    auto factory = new TestPersistFactory();
    DWORD registration = 0;
    HRESULT registered = CoRegisterClassObject(TestPersist::ClassId, factory, CLSCTX_INPROC_SERVER,
        REGCLS_MULTIPLEUSE, &registration);
    Check("ole_register_persistence_class", SUCCEEDED(registered));
    IPersistStream* loaded = nullptr;
    HRESULT hr = OleLoadFromStream(&stream, __uuidof(IPersistStream), reinterpret_cast<void**>(&loaded));
    Check("ole_load_registered_class", SUCCEEDED(hr) && loaded &&
        static_cast<TestPersist*>(loaded)->Value == object->Value);
    if (loaded) loaded->Release();
    stream.Bytes.resize(3); stream.Position = 0; loaded = reinterpret_cast<IPersistStream*>(1);
    hr = OleLoadFromStream(&stream, __uuidof(IPersistStream), reinterpret_cast<void**>(&loaded));
    Check("ole_load_short_read_clears_output", FAILED(hr) && !loaded);
    stream.ReadResult = E_ACCESSDENIED; stream.Position = 0;
    Check("ole_load_error_preserved", OleLoadFromStream(&stream, __uuidof(IPersistStream),
        reinterpret_cast<void**>(&loaded)) == E_ACCESSDENIED && !loaded);
    Check("ole_nonrunnable_object", SUCCEEDED(OleRun(object)) && SUCCEEDED(OleLockRunning(object, TRUE, FALSE)));
    if (SUCCEEDED(registered)) CoRevokeClassObject(registration);
    factory->Release(); object->Release();
    CoUninitialize();
}

class TestMediumOwner final : public IUnknown {
public:
    ULONG References = 1;
    ULONG Releases = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *output = static_cast<IUnknown*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++References; }
    ULONG STDMETHODCALLTYPE Release() override { ++Releases; return --References; }
};

class TestDataObject final : public IDataObject {
public:
    TestMediumOwner Owner;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDataObject)) return E_NOINTERFACE;
        *output = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC*, STGMEDIUM* medium) override {
        *medium = {}; medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = reinterpret_cast<HGLOBAL>(1);
        medium->pUnkForRelease = &Owner; Owner.AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

void TestDataAdvise() {
    IDataAdviseHolder* holder = nullptr;
    Require(SUCCEEDED(CreateDataAdviseHolder(&holder)) && holder, "create data advise holder");
    FORMATETC format{CF_TEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    TestAdviseSink sink;
    TestAdviseSink onceSink;
    TestDataObject object;
    DWORD first = 0, second = 0;
    Check("advise_register", SUCCEEDED(holder->Advise(&object, &format, ADVF_NODATA, &sink, &first)) &&
        first && sink.References == 2);
    Check("advise_only_once_prime", SUCCEEDED(holder->Advise(&object, &format,
        ADVF_NODATA | ADVF_PRIMEFIRST | ADVF_ONLYONCE, &onceSink, &second)) &&
        second == 0 && onceSink.Calls == 1 && onceSink.References == 1);
    Check("advise_empty_cookie_noop", holder->Unadvise(second) == S_OK);
    holder->SendOnDataChange(&object, 0, 0);
    Check("advise_first_change_after_prime", sink.Calls == 1 && onceSink.Calls == 1);
    Check("advise_only_once_registration", SUCCEEDED(holder->Advise(&object, &format,
        ADVF_NODATA | ADVF_ONLYONCE, &onceSink, &second)) && second != 0);
    holder->SendOnDataChange(&object, 0, 0);
    holder->SendOnDataChange(&object, 0, 0);
    Check("advise_send_once_removed", sink.Calls == 3 && onceSink.Calls == 2 && onceSink.References == 1);
    IEnumSTATDATA* enumeration = nullptr;
    Check("advise_enumeration", SUCCEEDED(holder->EnumAdvise(&enumeration)) && enumeration);
    if (enumeration) {
        STATDATA data[2]{}; ULONG fetched = 0;
        Check("advise_enumeration_ownership", enumeration->Next(2, data, &fetched) == S_FALSE &&
            fetched == 1 && data[0].dwConnection == first && data[0].pAdvSink == &sink && sink.References == 3);
        for (ULONG i = 0; i < fetched; ++i) {
            data[i].pAdvSink->Release(); CoTaskMemFree(data[i].formatetc.ptd);
        }
        enumeration->Reset();
        IEnumSTATDATA* clone = nullptr;
        Check("advise_enumeration_clone", SUCCEEDED(enumeration->Clone(&clone)) && clone);
        if (clone) {
            STATDATA item{};
            Check("advise_clone_independent", clone->Next(1, &item, nullptr) == S_OK && item.dwConnection == first);
            if (item.pAdvSink) item.pAdvSink->Release();
            CoTaskMemFree(item.formatetc.ptd); clone->Release();
        }
        enumeration->Release();
    }
    Check("advise_unadvise_releases", holder->Unadvise(first) == S_OK && sink.References == 1);
    sink.Holder = holder; sink.Cookie = &first;
    HRESULT reentrantResult = holder->Advise(&object, &format, ADVF_NODATA, &sink, &first);
    holder->SendOnDataChange(&object, 0, 0);
    Check("advise_reentrant_unadvise", SUCCEEDED(reentrantResult) && sink.References == 1);
    sink.Holder = nullptr; sink.Cookie = nullptr;
    DWORD dataCookie = 0;
    Check("advise_data_medium_registration", SUCCEEDED(holder->Advise(&object, &format, 0, &sink, &dataCookie)));
    holder->SendOnDataChange(&object, 0, 0);
    Check("advise_data_medium_ownership", object.Owner.References == 1 && object.Owner.Releases == 1);
    holder->Unadvise(dataCookie);
    TestAdviseSink ordinary, stopping;
    DWORD ordinaryCookie = 0, stoppingCookie = 0;
    Require(SUCCEEDED(holder->Advise(&object, &format, ADVF_NODATA, &ordinary, &ordinaryCookie)) &&
        SUCCEEDED(holder->Advise(&object, &format, ADVF_NODATA | ADVF_DATAONSTOP, &stopping, &stoppingCookie)),
        "register stop notifications");
    holder->SendOnDataChange(&object, 0, 0);
    Check("advise_normal_change_includes_stop_subscriptions",
        ordinary.Calls == 1 && stopping.Calls == 1 && !ordinary.DataCalls && !stopping.DataCalls);
    holder->SendOnDataChange(&object, 0, ADVF_DATAONSTOP);
    Check("advise_data_on_stop_selection_and_data",
        ordinary.Calls == 1 && stopping.Calls == 2 && stopping.DataCalls == 1 &&
        object.Owner.References == 1 && object.Owner.Releases == 2);
    holder->Unadvise(ordinaryCookie); holder->Unadvise(stoppingCookie);
    holder->Release();
}

void TestStorageMediumOwnership() {
    TestStream stream;
    TestMediumOwner owner;
    owner.AddRef();
    STGMEDIUM medium{};
    medium.tymed = TYMED_ISTREAM; medium.pstm = &stream; medium.pUnkForRelease = &owner;
    ReleaseStgMedium(&medium);
    Check("medium_release_stream_and_owner", owner.References == 1 && owner.Releases == 1 && stream.Releases == 1);
    medium = {}; medium.tymed = TYMED_ISTREAM; medium.pstm = &stream;
    ReleaseStgMedium(&medium);
    Check("medium_release_stream", stream.Releases == 2);
    HGLOBAL allocation = GlobalAlloc(GMEM_MOVEABLE, 16);
    Require(allocation != nullptr, "allocate storage medium");
    owner.AddRef();
    medium = {}; medium.tymed = TYMED_HGLOBAL; medium.hGlobal = allocation; medium.pUnkForRelease = &owner;
    ReleaseStgMedium(&medium);
    Check("medium_global_owner_is_exclusive", owner.References == 1 && owner.Releases == 2 &&
        GlobalFlags(allocation) != GMEM_INVALID_HANDLE);
    medium = {}; medium.tymed = TYMED_HGLOBAL; medium.hGlobal = allocation;
    ReleaseStgMedium(&medium);
    Check("medium_release_global", GlobalFlags(allocation) == GMEM_INVALID_HANDLE);
}

class TestDispenserDriver final : public IDispenserDriver {
public:
    ULONG References = 1;
    unsigned Created = 0, Reset = 0, Destroyed = 0, DestroyedStrings = 0;
    IHolder* ReentrantHolder = nullptr;
    HRESULT ResetResult = S_OK;
    HANDLE ResetEntered = nullptr, ContinueReset = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDispenserDriver)) return E_NOINTERFACE;
        *output = static_cast<IDispenserDriver*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++References; }
    ULONG STDMETHODCALLTYPE Release() override { return --References; }
    HRESULT STDMETHODCALLTYPE CreateResource(RESTYPID, RESID* output, TIMEINSECS* seconds) override {
        *output = ++Created; *seconds = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RateResource(RESTYPID, RESID, BOOL, RESOURCERATING* rating) override { *rating = 100; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnlistResource(RESID, TRANSID) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE ResetResource(RESID) override {
        ++Reset;
        if (ResetEntered) {
            SetEvent(ResetEntered);
            if (WaitForSingleObject(ContinueReset, 5000) != WAIT_OBJECT_0) return E_ABORT;
        }
        if (ReentrantHolder) ReentrantHolder->Close();
        return ResetResult;
    }
    HRESULT STDMETHODCALLTYPE DestroyResource(RESID) override { ++Destroyed; return S_OK; }
    HRESULT STDMETHODCALLTYPE DestroyResourceS(constSRESID) override { ++DestroyedStrings; return S_OK; }
};

void TestDispenser() {
    constexpr GUID clsid = {0xecabb0c0,0x7f19,0x11d2,{0x97,0x8e,0,0,0xf8,0x75,0x7e,0x2a}};
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Require(SUCCEEDED(init), "initialize dispenser apartment");
    IDispenserManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IDispenserManager),
        reinterpret_cast<void**>(&manager));
    Check("dispenser_native_activation", SUCCEEDED(hr) && manager);
    if (manager) {
        INSTID instanceId = 1; TRANSID transaction = 1;
        Check("dispenser_nontransactional_context", manager->GetContext(&instanceId, &transaction) == S_OK &&
            !instanceId && !transaction);
        TestDispenserDriver driver;
        IHolder* holder = nullptr;
        Require(SUCCEEDED(manager->RegisterDispenser(&driver, L"NativeRegression", &holder)) && holder,
            "register dispenser");
        Check("dispenser_owns_driver", driver.References == 2);
        RESID resource = 0;
        Check("dispenser_allocate", SUCCEEDED(holder->AllocResource(1, &resource)) && resource && driver.Created == 1);
        driver.ResetResult = E_ACCESSDENIED;
        Check("dispenser_reset_failure_retains_resource", holder->FreeResource(resource) == E_ACCESSDENIED && !driver.Destroyed);
        driver.ResetResult = S_OK;
        Check("dispenser_free_zero_retention", holder->FreeResource(resource) == S_OK &&
            driver.Reset == 2 && driver.Destroyed == 1 && holder->FreeResource(resource) == E_INVALIDARG);
        Check("dispenser_track_external", holder->TrackResource(100) == S_OK && holder->TrackResource(100) == E_INVALIDARG &&
            holder->UntrackResource(100, FALSE) == S_OK && driver.Destroyed == 1);
        Check("dispenser_track_string", holder->TrackResourceS(L"resource") == S_OK &&
            holder->UntrackResourceS(L"resource", TRUE) == S_OK && driver.DestroyedStrings == 1);
        holder->AllocResource(1, &resource);
        driver.ReentrantHolder = holder;
        Check("dispenser_reentrant_close", holder->FreeResource(resource) == S_OK && driver.Destroyed == 2 &&
            driver.References == 1);
        Check("dispenser_repeated_close", holder->Close() == S_OK && holder->Close() == S_OK &&
            holder->AllocResource(1, &resource) == CO_E_OBJNOTCONNECTED && resource == 0);
        holder->Release(); manager->Release();
        Check("dispenser_balanced_native_lifetime", driver.References == 1);
        manager = nullptr;
        Require(SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IDispenserManager),
            reinterpret_cast<void**>(&manager))), "create concurrent dispenser");
        TestDispenserDriver concurrent;
        concurrent.ResetEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        concurrent.ContinueReset = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        holder = nullptr;
        Require(SUCCEEDED(manager->RegisterDispenser(&concurrent, L"ConcurrentRegression", &holder)),
            "register concurrent dispenser");
        Require(SUCCEEDED(holder->AllocResource(1, &resource)), "allocate concurrent resource");
        HRESULT freeResult = E_FAIL;
        std::thread freeing([&] { freeResult = holder->FreeResource(resource); });
        bool entered = WaitForSingleObject(concurrent.ResetEntered, 5000) == WAIT_OBJECT_0;
        Check("dispenser_overlapping_operations_are_busy", entered &&
            holder->FreeResource(resource) == E_PENDING && holder->UntrackResource(resource, FALSE) == E_PENDING);
        Check("dispenser_close_defers_inflight_destruction", holder->Close() == S_OK &&
            !concurrent.Destroyed && concurrent.References == 2);
        SetEvent(concurrent.ContinueReset); freeing.join();
        Check("dispenser_inflight_completion_releases_once", freeResult == S_OK &&
            concurrent.Destroyed == 1 && concurrent.References == 1);
        holder->Release(); manager->Release();
        CloseHandle(concurrent.ResetEntered); CloseHandle(concurrent.ContinueReset);
    }
    CoUninitialize();
}

void TestAtomImports() {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring directory(executable);
    directory.resize(directory.find_last_of(L'\\'));
    std::wstring path = directory + L"\\AtomRegression.exe";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        path = directory + (sizeof(void*) == 4 ? L"\\AtomRegression_x86.exe" : L"\\AtomRegression_x64.exe");
    std::wstring command = L"\"" + path + L"\" --exhaust";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    bool launched = CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, directory.c_str(), &startup, &child) != FALSE;
    DWORD code = 1;
    if (launched) {
        if (WaitForSingleObject(child.hProcess, 30000) != WAIT_OBJECT_0) {
            TerminateProcess(child.hProcess, 2);
            WaitForSingleObject(child.hProcess, 5000);
        }
        GetExitCodeProcess(child.hProcess, &code);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    Check("atom_public_import_process", launched && code == 0);
}

void TestProviderLifetime() {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring directory(executable);
    directory.resize(directory.find_last_of(L'\\'));
    std::wstring path = directory + L"\\ProviderLifetimeRegression.exe";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        path = directory + (sizeof(void*) == 4 ?
            L"\\ProviderLifetimeRegression_x86.exe" : L"\\ProviderLifetimeRegression_x64.exe");
    std::wstring command = L"\"" + path + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    bool launched = CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, directory.c_str(), &startup, &child) != FALSE;
    DWORD code = 1;
    DWORD wait = WAIT_FAILED;
    if (launched) {
        wait = WaitForSingleObject(child.hProcess, 10000);
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(child.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(child.hProcess, 5000);
        }
        GetExitCodeProcess(child.hProcess, &code);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    std::printf("OBSERVE provider_lifetime launched=%d wait=%lu exit=0x%08lX\n", launched, wait, code);
    Check("provider_callback_owner_survives_last_explicit_release", launched && wait == WAIT_OBJECT_0 && !code);
}

HMODULE LoadTeardownFixture() {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    wchar_t* separator = wcsrchr(executable, L'\\');
    if (separator) separator[1] = L'\0';
    std::wstring path = std::wstring(executable) + (sizeof(void*) == 4 ?
        L"teardown_fixture_x86.dll" : L"teardown_fixture_x64.dll");
    return LoadLibraryW(path.c_str());
}

using ConfigureTeardownFn = BOOL (__cdecl*)(HANDLE, BOOL);
int RunTeardownChild(HANDLE completed) {
    HMODULE fixture = LoadTeardownFixture();
    auto configure = fixture ? reinterpret_cast<ConfigureTeardownFn>(
        GetProcAddress(fixture, "TeardownFixtureConfigure")) : nullptr;
    if (!configure || !configure(completed, FALSE)) {
        std::printf("OBSERVE teardown child configuration failed error=%lu\n", GetLastError());
        return 3;
    }
    // Do not FreeLibrary: this specifically exercises DLL_PROCESS_DETACH.
    ExitProcess(0);
}

void TestDispenserTeardown() {
    HMODULE fixture = LoadTeardownFixture();
    Require(fixture != nullptr, "load native teardown fixture");
    auto configure = reinterpret_cast<ConfigureTeardownFn>(GetProcAddress(fixture, "TeardownFixtureConfigure"));
    using StatusFn = DWORD (__cdecl*)();
    auto status = reinterpret_cast<StatusFn>(GetProcAddress(fixture, "TeardownFixtureStatus"));
    Require(configure && status && configure(nullptr, TRUE), "configure native teardown fixture");
    std::thread worker([] {});
    worker.join();
    DWORD result = status();
    Check("dispenser_dll_thread_detach_close_release", (result & 0xFFFF) != 0 && (result >> 16) == 0);
    FreeLibrary(fixture);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE completed = CreateEventW(&security, TRUE, FALSE, nullptr);
    Require(completed != nullptr, "create teardown completion event");
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --teardown-child " +
        std::to_wstring(reinterpret_cast<ULONG_PTR>(completed));
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    bool launched = CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, 0,
        nullptr, nullptr, &startup, &child) != FALSE;
    DWORD launchError = launched ? ERROR_SUCCESS : GetLastError();
    DWORD code = 1;
    DWORD wait = WAIT_FAILED;
    if (launched) {
        wait = WaitForSingleObject(child.hProcess, 10000);
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(child.hProcess, 2); WaitForSingleObject(child.hProcess, 5000);
        }
        GetExitCodeProcess(child.hProcess, &code);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    DWORD signaled = WaitForSingleObject(completed, 0);
    std::printf("OBSERVE teardown launched=%d create_error=%lu wait=%lu exit=0x%08lX event=%lu\n",
        launched, launchError, wait, code, signaled);
    Check("dispenser_dll_process_detach_close_release", launched && !code && signaled == WAIT_OBJECT_0);
    CloseHandle(completed);
    HMODULE server = LoadLibraryExW(L"comsvcs.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using CanUnloadFn = HRESULT (WINAPI*)();
    auto canUnload = server ? reinterpret_cast<CanUnloadFn>(GetProcAddress(server, "DllCanUnloadNow")) : nullptr;
    HMODULE owner = nullptr;
    bool resident = canUnload && canUnload() == S_FALSE && GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(canUnload), &owner) &&
        owner == GetModuleHandleW(L"nanoserver_sta_shim.dll");
    if (server) FreeLibrary(server);
    Check("dispenser_callback_code_remains_resident", resident && owner == GetModuleHandleW(L"nanoserver_sta_shim.dll"));
}

void TestLegacyCompatibility(bool requireShim) {
    TestOleInitialization();
    TestOlePersistence();
    TestDataAdvise();
    TestStorageMediumOwnership();
    wchar_t copy[16] = L"???????????????";
    StrCpyNW(copy, L"abcdef", 4);
    Check("shell_bounded_copy", wcscmp(copy, L"abc") == 0 && copy[4] == L'?');
    StrNCatW(copy, L"uvwxyz", 4);
    Check("shell_append_count_matches_windows", wcscmp(copy, L"abcuvw") == 0 && copy[7] == L'?');
    StrCpyW(copy, L"abc"); StrCatW(copy, L"def");
    Check("shell_copy_and_cat", wcscmp(copy, L"abcdef") == 0);
    HMODULE shell = LoadLibraryW(L"shlwapi.dll");
    Require(shell != nullptr, "load shell helper");
    struct OrdinalControl { WORD Ordinal; const wchar_t* Module; const char* Name; };
    const OrdinalControl ordinals[] = {
        {28,L"user32.dll","IsCharAlphaNumericW"},{38,L"user32.dll","CharLowerW"},
        {44,L"user32.dll","CharUpperBuffW"},{45,L"kernel32.dll","CompareStringW"},
        {52,L"kernel32.dll","CreateFileW"},{66,L"kernel32.dll","FindResourceW"},
        {115,L"kernel32.dll","OutputDebugStringW"},{159,L"kernel32.dll","CompareStringW"},
        {310,L"kernel32.dll","GetTimeFormatW"},{311,L"kernel32.dll","GetDateFormatW"},
        {sizeof(void*) == 4 ? 838 : 843,L"shlwapi.dll","StrCatW"},
        {sizeof(void*) == 4 ? 852 : 857,L"shlwapi.dll","StrCpyNW"},
        {sizeof(void*) == 4 ? 853 : 858,L"shlwapi.dll","StrCpyW"},
        {sizeof(void*) == 4 ? 867 : 872,L"shlwapi.dll","StrNCatW"}
    };
    for (const auto& entry : ordinals) {
        auto actual = GetProcAddress(shell, MAKEINTRESOURCEA(entry.Ordinal));
        auto expected = GetProcAddress(GetModuleHandleW(entry.Module), entry.Name);
        std::printf("OBSERVE shell ordinal=%u target=%s\n", entry.Ordinal, entry.Name);
        Check("shell_classic_ordinal_abi", actual && actual == expected);
    }
    Check("shell_preserves_unrelated_exports", GetProcAddress(shell, "PathFileExistsW") &&
        GetProcAddress(shell, "StrChrW"));
    using PathExistsFn = BOOL (WINAPI*)(LPCWSTR);
    auto pathExists = reinterpret_cast<PathExistsFn>(GetProcAddress(shell, "PathFileExistsW"));
    Check("shell_preserves_unrelated_export_abi", pathExists && pathExists(L"C:\\Windows") &&
        !pathExists(L"C:\\__NanoRegressionMissingDirectory__"));
    FreeLibrary(shell);
    if (!requireShim) return;
    SIZE size{}; int fit = -1;
    int advances[] = {-1,-1,-1,0x76543210};
    Check("text_extent_fit_and_guard", GetTextExtentExPointA(nullptr, "abcd", 4, 16, &fit, advances, &size) &&
        fit == 2 && advances[0] == 8 && advances[1] == 16 && advances[2] == -1 &&
        advances[3] == 0x76543210 && size.cx == 32 && size.cy == 16);
    Check("text_extent_empty", GetTextExtentExPointA(nullptr, nullptr, 0, 0, &fit, nullptr, &size) &&
        fit == 0 && size.cx == 0 && size.cy == 16);
    Check("text_extent_negative_rejected", !GetTextExtentExPointA(nullptr, "x", -1, 0, nullptr, nullptr, &size));
    Check("text_extent_overflow_rejected", !GetTextExtentExPointA(nullptr, "x", INT_MAX, 0, nullptr, nullptr, &size));
    Check("text_extent_null_text_rejected", !GetTextExtentExPointA(nullptr, nullptr, 1, 0, nullptr, nullptr, &size));
    Check("text_extent_optional_outputs", GetTextExtentExPointA(nullptr, "abc", 3, 0, nullptr, advances, &size) &&
        advances[0] == 8 && advances[1] == 16 && advances[2] == 24 && advances[3] == 0x76543210);
    HMODULE olepro = LoadLibraryExW(L"olepro32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    Check("legacy_olepro_forwarder", olepro != nullptr);
    if (olepro) {
        const char* names[] = {"OleIconToCursor","OleCreatePropertyFrameIndirect","OleCreatePropertyFrame",
            "OleLoadPicture","OleCreatePictureIndirect","OleCreateFontIndirect","OleTranslateColor",
            "DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer"};
        for (WORD i = 0; i < _countof(names); ++i) {
            auto named = GetProcAddress(olepro, names[i]);
            MEMORY_BASIC_INFORMATION region{};
            bool executable = named && VirtualQuery(reinterpret_cast<PVOID>(named), &region, sizeof(region)) &&
                region.State == MEM_COMMIT && (region.Protect &
                    (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            Check("legacy_olepro_name_and_ordinal", executable &&
                named == GetProcAddress(olepro, MAKEINTRESOURCEA(248 + i)));
        }
        FreeLibrary(olepro);
    }
    TestDispenser();
    TestDispenserTeardown();
    TestAtomImports();
    TestProviderLifetime();
}
