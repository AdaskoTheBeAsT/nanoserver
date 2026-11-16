#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllimport) ATOM WINAPI AtomFixtureProbe();
int main() { return AtomFixtureProbe() ? 0 : 1; }
