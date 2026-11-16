// Optional private-runtime qualification. No runtime binary is patched.
// Hardware breakpoints trace the real activation queries during DllMain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <map>

template<class T> bool Remote(HANDLE process, ULONG_PTR address, T& value) {
    return ReadProcessMemory(process, reinterpret_cast<void*>(address), &value, sizeof(value), nullptr) != FALSE;
}

ULONG_PTR RemoteImport(HANDLE process, ULONG_PTR image, const char* wanted) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS headers{};
    if (!Remote(process, image, dos) || !Remote(process, image + dos.e_lfanew, headers)) return 0;
    ULONG_PTR descriptors = image + headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    for (unsigned i = 0; i < 128; ++i) {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!Remote(process, descriptors + i * sizeof(descriptor), descriptor) || !descriptor.Name) break;
        if (!descriptor.OriginalFirstThunk) continue;
        for (unsigned j = 0; j < 4096; ++j) {
            ULONG_PTR nameRva = 0, target = 0;
            if (!Remote(process, image + descriptor.OriginalFirstThunk + j * sizeof(ULONG_PTR), nameRva) || !nameRva) break;
            if (IMAGE_SNAP_BY_ORDINAL(nameRva)) continue;
            char name[128]{};
            if (!ReadProcessMemory(process, reinterpret_cast<void*>(image + nameRva + sizeof(WORD)),
                name, sizeof(name) - 1, nullptr)) continue;
            if (!strcmp(name, wanted) && Remote(process, image + descriptor.FirstThunk + j * sizeof(ULONG_PTR), target))
                return target;
        }
    }
    return 0;
}

std::string RemoteModuleName(HANDLE process, ULONG_PTR address) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQueryEx(process, reinterpret_cast<void*>(address), &memory, sizeof(memory))) return "unknown";
    ULONG_PTR image = reinterpret_cast<ULONG_PTR>(memory.AllocationBase);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS headers{};
    IMAGE_EXPORT_DIRECTORY exports{};
    char name[64]{};
    if (!Remote(process, image, dos) || !Remote(process, image + dos.e_lfanew, headers) ||
        !Remote(process, image + headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress, exports) ||
        !ReadProcessMemory(process, reinterpret_cast<void*>(image + exports.Name), name, sizeof(name) - 1, nullptr))
        return "unknown";
    return name;
}

int Probe(const wchar_t* directory) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::wstring root(directory);
    std::wstring manifest = root + L"\\Microsoft.VC90.CRT.manifest";
    ACTCTXW description{sizeof(description)};
    description.dwFlags = ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID;
    description.lpSource = manifest.c_str();
    description.lpAssemblyDirectory = root.c_str();
    HANDLE context = CreateActCtxW(&description);
    std::printf("OBSERVE private_manifest_created=%d error=%lu\n", context != INVALID_HANDLE_VALUE, GetLastError());
    ULONG_PTR cookie = 0;
    bool activated = context != INVALID_HANDLE_VALUE && ActivateActCtx(context, &cookie);
    std::printf("OBSERVE private_manifest_activated=%d error=%lu\n", activated, GetLastError());
    std::puts("OBSERVE loading_unmodified_msvcr90");
    HMODULE crt = LoadLibraryExW((root + L"\\msvcr90.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    std::printf("OBSERVE msvcr90_loaded=%d error=%lu\n", crt != nullptr, GetLastError());
    bool success = crt != nullptr;
    if (crt) {
        wchar_t loadedPath[MAX_PATH]{};
        std::wstring expectedPath = root + L"\\msvcr90.dll";
        bool privatePath = GetModuleFileNameW(crt, loadedPath, MAX_PATH) &&
            !_wcsicmp(expectedPath.c_str(), loadedPath);
        std::printf("OBSERVE msvcr90_private_path=%d\n", privatePath);
        using AllocateFn = void* (__cdecl*)(size_t);
        using FreeFn = void (__cdecl*)(void*);
        using SqrtFn = double (__cdecl*)(double);
        auto allocate = reinterpret_cast<AllocateFn>(GetProcAddress(crt, "malloc"));
        auto freeMemory = reinterpret_cast<FreeFn>(GetProcAddress(crt, "free"));
        auto squareRoot = reinterpret_cast<SqrtFn>(GetProcAddress(crt, "sqrt"));
        void* allocation = allocate && freeMemory ? allocate(32) : nullptr;
        success = privatePath && allocation && squareRoot && squareRoot(4.0) == 2.0;
        if (allocation) freeMemory(allocation);
        HMODULE cpp = LoadLibraryExW((root + L"\\msvcp90.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        std::printf("OBSERVE msvcp90_loaded=%d error=%lu\n", cpp != nullptr, GetLastError());
        bool cppExecuted = false;
        if (cpp) {
            using SyncFn = bool (__cdecl*)(bool);
            auto sync = reinterpret_cast<SyncFn>(GetProcAddress(cpp,
                "?sync_with_stdio@ios_base@std@@SA_N_N@Z"));
            if (sync) {
                bool original = sync(true);
                bool afterTrue = sync(false);
                bool afterFalse = sync(original);
                cppExecuted = afterTrue && !afterFalse;
            }
        }
        std::printf("OBSERVE msvcp90_cpp_execution=%d\n", cppExecuted);
        success = success && cppExecuted;
        if (cpp) FreeLibrary(cpp);
        FreeLibrary(crt);
    }
    if (activated) DeactivateActCtx(0, cookie);
    if (context != INVALID_HANDLE_VALUE) ReleaseActCtx(context);
    std::printf("SUMMARY private_crt=%s\n", success ? "PASS" : "FAIL");
    return success ? 0 : 1;
}

int wmain(int argc, wchar_t* argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::puts("Usage: CrtRegression.exe <private Microsoft.VC90.CRT directory>");
        return 64;
    }
    if (argc > 2 && wcscmp(argv[2], L"--child") == 0) {
        HMODULE user = LoadLibraryW(L"user32.dll");
        using DesktopFn = HWND (WINAPI*)();
        auto desktop = user ? reinterpret_cast<DesktopFn>(GetProcAddress(user, "GetDesktopWindow")) : nullptr;
        if (!desktop) return 3;
        desktop();
        return Probe(argv[1]);
    }
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" \"" + argv[1] + L"\" --child";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
        DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &startup, &child)) {
        std::printf("FATAL cannot_debug_private_runtime error=%lu\n", GetLastError());
        return 2;
    }
    ULONG_PTR query = reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryActCtxW"));
    ULONG_PTR nativeQuery = reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlQueryInformationActivationContext"));
    ULONG_PTR lookup = reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetProcAddress"));
    ULONG_PTR findSection = reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FindActCtxSectionStringW"));
    bool armed = false, exited = false;
    ULONG_PTR crtImage = 0, crtEntry = 0;
    std::map<DWORD, HANDLE> threads{{child.dwThreadId, child.hThread}};
    std::map<DWORD, bool> pendingLookup;
    DWORD code = 2;
    ULONGLONG deadline = GetTickCount64() + 30000;
    while (!exited && GetTickCount64() < deadline) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 1000)) continue;
        DWORD continuation = DBG_CONTINUE;
        HANDLE eventThread = threads.count(event.dwThreadId) ? threads[event.dwThreadId] : child.hThread;
        if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            DWORD exception = event.u.Exception.ExceptionRecord.ExceptionCode;
            continuation = DBG_EXCEPTION_NOT_HANDLED;
            if (exception == EXCEPTION_BREAKPOINT && !armed) {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(eventThread, &context)) {
                    context.Dr0 = query; context.Dr1 = nativeQuery;
                    context.Dr2 = lookup; context.Dr3 = findSection; context.Dr7 = 0x55;
                    armed = SetThreadContext(eventThread, &context) != FALSE;
                }
                std::printf("OBSERVE activation_trace_armed=%d error=%lu\n", armed, GetLastError());
                continuation = DBG_CONTINUE;
            } else if (exception == EXCEPTION_SINGLE_STEP && armed) {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(eventThread, &context)) {
                    ULONG_PTR stack = 0, instruction = 0;
#ifdef _WIN64
                    stack = context.Rsp; instruction = context.Rip;
#else
                    stack = context.Esp; instruction = context.Eip;
#endif
                    if ((context.Dr6 & 1) && crtEntry && instruction == crtEntry) {
                        auto actual = RemoteImport(child.hProcess, crtImage, "GetProcAddress");
                        std::printf("TRACE CRT_DllMain_entered import_lookup_found=%d\n", actual != 0);
                        std::printf("TRACE CRT_lookup_provider=%s\n", RemoteModuleName(child.hProcess, actual).c_str());
                        if (actual) context.Dr2 = actual;
                        context.Dr0 = query;
                    } else if ((context.Dr6 & 8) && pendingLookup[event.dwThreadId]) {
#ifdef _WIN64
                        ULONG_PTR target = context.Rax;
#else
                        ULONG_PTR target = context.Eax;
#endif
                        std::printf("TRACE dynamic_FindActCtx_provider=%s\n",
                            target ? RemoteModuleName(child.hProcess, target).c_str() : "absent");
                        context.Dr3 = target; pendingLookup[event.dwThreadId] = false;
                    } else if (context.Dr6 & 0xF) {
                        ULONG flags = 0, infoClass = 0;
                        ULONG_PTR nameAddress = 0;
#ifdef _WIN64
                        flags = static_cast<ULONG>(context.Rcx); infoClass = static_cast<ULONG>(context.R9);
                        nameAddress = context.Rdx;
                        if (context.Dr6 & 8) infoClass = static_cast<ULONG>(context.R8);
#else
                        ULONG arguments[4]{};
                        ReadProcessMemory(child.hProcess, reinterpret_cast<void*>(stack + sizeof(ULONG_PTR)),
                            arguments, sizeof(arguments), nullptr);
                        flags = arguments[0]; infoClass = arguments[3];
                        nameAddress = arguments[1];
                        if (context.Dr6 & 8) infoClass = arguments[2];
#endif
                        if (context.Dr6 & 4) {
                            char name[128]{};
                            if (nameAddress > 0xFFFF && ReadProcessMemory(child.hProcess,
                                reinterpret_cast<void*>(nameAddress), name, sizeof(name) - 1, nullptr)) {
                                std::printf("TRACE GetProcAddress name=%s\n", name);
                                if (!strcmp(name, "FindActCtxSectionStringW")) {
                                    ULONG_PTR returnAddress = 0;
                                    if (Remote(child.hProcess, stack, returnAddress)) {
                                        context.Dr3 = returnAddress;
                                        pendingLookup[event.dwThreadId] = true;
                                    }
                                }
                            }
                        } else {
                            const char* api = context.Dr6 & 8 ? "FindActCtxSectionStringW" :
                                context.Dr6 & 2 ? "RtlQueryInformationActivationContext" : "QueryActCtxW";
                            std::printf("TRACE %s flags=%08lX class_or_section=%lu\n", api, flags, infoClass);
                        }
                    }
                    context.Dr6 = 0; context.Dr7 = 0x55;
                    context.EFlags |= 0x10000; // Resume past the hardware breakpoint.
                    SetThreadContext(eventThread, &context);
                    continuation = DBG_CONTINUE;
                }
            } else {
                const auto& record = event.u.Exception.ExceptionRecord;
                std::printf("TRACE exception=%08lX first_chance=%lu detail=%llu provider=%s\n",
                    exception, event.u.Exception.dwFirstChance,
                    static_cast<unsigned long long>(record.NumberParameters ? record.ExceptionInformation[0] : 0),
                    RemoteModuleName(child.hProcess, reinterpret_cast<ULONG_PTR>(record.ExceptionAddress)).c_str());
            }
        } else if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
        } else if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
            threads[event.dwThreadId] = event.u.CreateThread.hThread;
            if (armed) {
                CONTEXT context{}; context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(event.u.CreateThread.hThread, &context)) {
                    context.Dr0 = crtEntry ? crtEntry : query; context.Dr1 = nativeQuery;
                    context.Dr2 = lookup; context.Dr3 = findSection; context.Dr7 = 0x55;
                    SetThreadContext(event.u.CreateThread.hThread, &context);
                }
            }
        } else if (event.dwDebugEventCode == EXIT_THREAD_DEBUG_EVENT) {
            if (eventThread != child.hThread) { CloseHandle(eventThread); threads.erase(event.dwThreadId); }
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            ULONG_PTR image = reinterpret_cast<ULONG_PTR>(event.u.LoadDll.lpBaseOfDll);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS headers{};
            IMAGE_EXPORT_DIRECTORY exports{};
            char name[64]{};
            if (Remote(child.hProcess, image, dos) && Remote(child.hProcess, image + dos.e_lfanew, headers) &&
                Remote(child.hProcess, image + headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress, exports) &&
                ReadProcessMemory(child.hProcess, reinterpret_cast<void*>(image + exports.Name), name, sizeof(name) - 1, nullptr)) {
                if (!_stricmp(name, "msvcr90.dll") || !_stricmp(name, "msvcp90.dll")) {
                    crtImage = image;
                    crtEntry = crtImage + headers.OptionalHeader.AddressOfEntryPoint;
                    for (const auto& thread : threads) {
                        CONTEXT context{}; context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        if (GetThreadContext(thread.second, &context)) {
                            context.Dr0 = crtEntry; context.Dr7 = 0x55;
                            SetThreadContext(thread.second, &context);
                        }
                    }
                    std::printf("TRACE private_runtime_mapped=%s\n", name);
                }
            }
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            code = event.u.ExitProcess.dwExitCode; exited = true;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation);
    }
    if (!exited) { TerminateProcess(child.hProcess, 2); WaitForSingleObject(child.hProcess, 5000); }
    for (const auto& thread : threads) if (thread.second != child.hThread) CloseHandle(thread.second);
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    std::printf("SUMMARY traced_crt_exit=%lu\n", code);
    return static_cast<int>(code);
}
