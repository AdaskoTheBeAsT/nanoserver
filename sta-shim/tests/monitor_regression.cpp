// Tests use a fake SCM and clock. No real services are opened or changed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <initializer_list>

namespace {
enum class Scenario { Normal, ManagerDenied, OpenDenied, StartDenied, Timeout, QueryFailure, WasStopped, WebStopped };
Scenario scenario = Scenario::Normal;
DWORD states[2]{};
ULONGLONG clockMs = 0;
int starts = 0, stops = 0, configChanges = 0, closes = 0, failures = 0;

SC_HANDLE WINAPI FakeOpenManager(LPCWSTR, LPCWSTR, DWORD) {
    if (scenario == Scenario::ManagerDenied) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return reinterpret_cast<SC_HANDLE>(1);
}
SC_HANDLE WINAPI FakeOpenService(SC_HANDLE, LPCWSTR name, DWORD) {
    if (scenario == Scenario::OpenDenied) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return reinterpret_cast<SC_HANDLE>(static_cast<ULONG_PTR>(wcscmp(name, L"WAS") == 0 ? 2 : 3));
}
BOOL WINAPI FakeQuery(SC_HANDLE handle, SC_STATUS_TYPE, LPBYTE output, DWORD, LPDWORD needed) {
    if (scenario == Scenario::QueryFailure) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    auto* status = reinterpret_cast<SERVICE_STATUS_PROCESS*>(output);
    *status = {};
    size_t index = reinterpret_cast<ULONG_PTR>(handle) - 2;
    status->dwCurrentState = states[index];
    if (clockMs >= 1000 && ((index == 0 && scenario == Scenario::WasStopped) ||
                          (index == 1 && scenario == Scenario::WebStopped))) status->dwCurrentState = SERVICE_STOPPED;
    *needed = sizeof(*status);
    return TRUE;
}
BOOL WINAPI FakeStart(SC_HANDLE handle, DWORD, LPCWSTR*) {
    ++starts;
    if (scenario == Scenario::StartDenied) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    states[reinterpret_cast<ULONG_PTR>(handle) - 2] =
        scenario == Scenario::Timeout ? SERVICE_START_PENDING : SERVICE_RUNNING;
    return TRUE;
}
BOOL WINAPI FakeChangeConfig2(SC_HANDLE, DWORD level, LPVOID info) {
    if (level == SERVICE_CONFIG_FAILURE_ACTIONS && info) {
        auto* actions = static_cast<SERVICE_FAILURE_ACTIONSW*>(info);
        if (actions->cActions == 1 && actions->lpsaActions &&
            actions->lpsaActions[0].Type == SC_ACTION_NONE) {
            ++configChanges;
        }
    }
    return TRUE;
}
HANDLE WINAPI FakeCreateSnapshot(DWORD, DWORD) { return INVALID_HANDLE_VALUE; }
BOOL WINAPI FakeCloseService(SC_HANDLE) { ++closes; return TRUE; }
void WINAPI FakeSleep(DWORD interval) { clockMs += interval; }
ULONGLONG WINAPI FakeClock() { return clockMs; }
} // namespace

#define OpenSCManagerW FakeOpenManager
#define OpenServiceW FakeOpenService
#define QueryServiceStatusEx FakeQuery
#define StartServiceW FakeStart
#define ChangeServiceConfig2W FakeChangeConfig2
#define CreateToolhelp32Snapshot FakeCreateSnapshot
#define CloseServiceHandle FakeCloseService
#define Sleep FakeSleep
#define GetTickCount64 FakeClock
#define wmain MonitorUnderTest
#include "../iis_service_monitor.cpp"
#undef wmain

namespace {
void Reset(Scenario next) {
    scenario = next;
    states[0] = states[1] = SERVICE_STOPPED;
    clockMs = 0;
    starts = stops = configChanges = closes = 0;
}
void Check(const char* name, bool success) {
    std::printf("CHECK %s %s\n", name, success ? "PASS" : "FAIL");
    if (!success) ++failures;
}
} // namespace

int main() {
    Reset(Scenario::Normal);
    Check("R14_start_stopped_service", StartAndWait(reinterpret_cast<SC_HANDLE>(2), L"WAS") && starts == 1);
    Reset(Scenario::Normal); states[0] = SERVICE_RUNNING;
    Check("R14_already_running", StartAndWait(reinterpret_cast<SC_HANDLE>(2), L"WAS") && starts == 0);
    Reset(Scenario::ManagerDenied);
    Check("R14_manager_denied", MonitorIis() == 1 && closes == 0);
    Reset(Scenario::OpenDenied);
    Check("R14_open_denied", MonitorIis() == 1 && closes == 1);
    Reset(Scenario::StartDenied);
    Check("R14_start_denied", MonitorIis() == 1 && starts == 1 && closes == 2);
    Reset(Scenario::Timeout);
    Check("R14_start_timeout", MonitorIis() == 1 && clockMs == 30000 && closes == 2);
    Reset(Scenario::QueryFailure);
    Check("R14_query_failure", MonitorIis() == 1 && closes == 2);
    for (Scenario stopped : {Scenario::WasStopped, Scenario::WebStopped}) {
        Reset(stopped); states[0] = states[1] = SERVICE_RUNNING;
        Check(stopped == Scenario::WasStopped ? "R14_WAS_stopped" : "R14_W3SVC_stopped",
            MonitorIis() == 1 && clockMs == 1000 && configChanges == 2 && closes == 3);
    }
    std::printf("SUMMARY monitor failures=%d\n", failures);
    return failures ? 1 : 0;
}
