// Foreground IIS service monitor for Nano Server, which has no PowerShell.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

namespace {

void DisableFailureRestart(SC_HANDLE service) {
    SC_ACTION noneAction{SC_ACTION_NONE, 0};
    SERVICE_FAILURE_ACTIONSW actions{};
    actions.cActions = 1;
    actions.lpsaActions = &noneAction;
    ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &actions);
}

void TerminateProcessById(DWORD pid) {
    if (pid <= 4 || pid == GetCurrentProcessId()) return;
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (process) {
        TerminateProcess(process, 1);
        CloseHandle(process);
    }
}

void TerminateWorkerProcesses() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"w3wp.exe") == 0) {
                TerminateProcessById(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

bool ReadStatus(SC_HANDLE service, const wchar_t* name, SERVICE_STATUS_PROCESS& status) {
    DWORD needed = 0;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &needed)) return true;
    fwprintf(stderr, L"[ERR] Cannot query %s (err=%lu).\n", name, GetLastError());
    return false;
}

bool StartAndWait(SC_HANDLE service, const wchar_t* name) {
    SERVICE_STATUS_PROCESS status{};
    if (!ReadStatus(service, name, status)) return false;
    if (status.dwCurrentState == SERVICE_RUNNING) return true;
    if (status.dwCurrentState != SERVICE_START_PENDING &&
        !StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        fwprintf(stderr, L"[ERR] Cannot start %s (err=%lu).\n", name, GetLastError());
        return false;
    }
    ULONGLONG started = GetTickCount64();
    do {
        if (!ReadStatus(service, name, status)) return false;
        if (status.dwCurrentState == SERVICE_RUNNING) {
            wprintf(L"[OK] %s is running.\n", name);
            fflush(stdout);
            return true;
        }
        if (status.dwCurrentState != SERVICE_START_PENDING) break;
        Sleep(200);
    } while (GetTickCount64() - started < 30000);
    fwprintf(stderr, L"[ERR] %s did not start (state=%lu, win32Exit=%lu, serviceExit=%lu).\n",
        name, status.dwCurrentState, status.dwWin32ExitCode, status.dwServiceSpecificExitCode);
    return false;
}

int MonitorIis() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        fwprintf(stderr, L"[ERR] Cannot open service manager (err=%lu).\n", GetLastError());
        return 1;
    }
    const wchar_t* names[] = {L"WAS", L"W3SVC"};
    SC_HANDLE services[2]{};
    bool monitoring = false;
    int result = 1;
    for (size_t i = 0; i < 2; ++i) {
        services[i] = OpenServiceW(manager, names[i],
            SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | SERVICE_CHANGE_CONFIG);
        if (!services[i]) {
            fwprintf(stderr, L"[ERR] Cannot open %s (err=%lu).\n", names[i], GetLastError());
            goto cleanup;
        }
        DisableFailureRestart(services[i]);
        if (!StartAndWait(services[i], names[i])) goto cleanup;
    }
    monitoring = true;
    wprintf(L"[OK] Monitoring WAS and W3SVC. Container exits if either service stops.\n");
    fflush(stdout);
    for (;;) {
        for (size_t i = 0; i < 2; ++i) {
            SERVICE_STATUS_PROCESS status{};
            if (!ReadStatus(services[i], names[i], status)) goto cleanup;
            if (status.dwCurrentState != SERVICE_RUNNING) {
                fwprintf(stderr, L"[ERR] %s stopped (state=%lu, win32Exit=%lu, serviceExit=%lu).\n",
                    names[i], status.dwCurrentState, status.dwWin32ExitCode, status.dwServiceSpecificExitCode);
                goto cleanup;
            }
        }
        Sleep(1000);
    }
cleanup:
    fflush(stdout);
    fflush(stderr);
    if (monitoring) {
        TerminateWorkerProcesses();
        for (size_t i = 2; i > 0; --i) {
            if (services[i - 1]) {
                SERVICE_STATUS_PROCESS status{};
                DWORD needed = 0;
                if (QueryServiceStatusEx(services[i - 1], SC_STATUS_PROCESS_INFO,
                        reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed) &&
                    status.dwCurrentState != SERVICE_STOPPED && status.dwProcessId > 4) {
                    TerminateProcessById(status.dwProcessId);
                }
            }
        }
    }
    for (SC_HANDLE service : services) {
        if (service) CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return result;
}

} // namespace

int wmain() {
    return MonitorIis();
}
