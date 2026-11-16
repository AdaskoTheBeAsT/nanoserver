#define WIN32_LEAN_AND_MEAN
#include <windows.h>

ATOM atom = 0;
extern "C" __declspec(dllexport) ATOM WINAPI AtomFixtureProbe() { return atom; }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        GetDesktopWindow();
        atom = GlobalAddAtomA("NanoDllMainAtom");
        return atom != 0;
    }
    if (reason == DLL_PROCESS_DETACH && atom) GlobalDeleteAtom(atom);
    return TRUE;
}
