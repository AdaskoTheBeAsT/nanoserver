// Internal hook safety checks complement, but do not replace, the normal-import
// regression. Only pages allocated by this test are patched.
#include "../nanoserver_sta_shim.cpp"
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
int failures = 0;
void Check(const char* name, bool success) {
    std::printf("CHECK %s %s\n", name, success ? "PASS" : "FAIL");
    if (!success) ++failures;
}
DWORD Protection(void* address) {
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(address, &memory, sizeof(memory)) ? memory.Protect : 0;
}
void Relative(BYTE* location, const BYTE* origin, const void* target) {
    int displacement = static_cast<int>(static_cast<const BYTE*>(target) - origin);
    memcpy(location, &displacement, sizeof(displacement));
}
} // namespace

int main() {
    auto* memory = static_cast<BYTE*>(VirtualAlloc(nullptr, 0x4000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    ModuleHookState state;
    state.ModuleBase = reinterpret_cast<HMODULE>(memory);
    state.ImageSize = 0x4000;
    IMAGE_SECTION_HEADER sections[3]{};
    sections[0].VirtualAddress = 0x1000;
    sections[0].SizeOfRawData = sections[0].Misc.VirtualSize = 0x1000;
    sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    sections[1].VirtualAddress = 0x2000;
    sections[1].SizeOfRawData = sections[1].Misc.VirtualSize = 0x1000;
    sections[1].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    sections[2].VirtualAddress = 0x3000;
    sections[2].SizeOfRawData = sections[2].Misc.VirtualSize = 0x1000;
    sections[2].Characteristics = IMAGE_SCN_MEM_READ;
    auto* cache = reinterpret_cast<int*>(memory + 0x2080);
    auto* text = reinterpret_cast<UnicodeStringHeader*>(memory + 0x3100);
    auto* buffer = reinterpret_cast<wchar_t*>(memory + 0x3200);
    const wchar_t name[] = L"ext-ms-win-ntuser-window-l1-1-0";
    memcpy(buffer, name, sizeof(name));
    text->Length = sizeof(name) - sizeof(wchar_t);
    text->MaximumLength = sizeof(name);
    text->Buffer = buffer;
    BYTE* instruction = memory + 0x1080;
    *reinterpret_cast<DWORD*>(instruction) = 0xD21BD9F6U;
    *reinterpret_cast<DWORD*>(instruction + 4) = 0x8902C283U;
    instruction[8] = 0x15;
    instruction[-48] = 0x8B; instruction[-47] = 0x0D;
    Relative(instruction - 46, instruction - 42, cache);
    instruction[-22] = 0x48; instruction[-21] = 0x8D; instruction[-20] = 0x0D;
    Relative(instruction - 19, instruction - 15, text);
    Relative(instruction + 9, instruction + 13, cache);
    DWORD previous = 0;
    if (!VirtualProtect(memory + 0x1000, 0x1000, PAGE_EXECUTE_READ, &previous)) return 2;
    DiscoverPresenceCaches(state, sections, 3);
    Check("R19_accept_valid_cache", state.KnownPresenceCacheAddresses.size() == 1 &&
        state.KnownPresenceCacheAddresses[0] == cache);
    state.KnownPresenceCacheAddresses.clear();
    text->Buffer = reinterpret_cast<wchar_t*>(memory + 0x5000);
    DiscoverPresenceCaches(state, sections, 3);
    Check("R19_reject_out_of_image_buffer", state.KnownPresenceCacheAddresses.empty());
    text->Buffer = buffer;
    text->Length = 1;
    DiscoverPresenceCaches(state, sections, 3);
    Check("R19_reject_odd_string_length", state.KnownPresenceCacheAddresses.empty());
    text->Length = sizeof(name) - sizeof(wchar_t);
    sections[1].Characteristics |= IMAGE_SCN_MEM_EXECUTE;
    DiscoverPresenceCaches(state, sections, 3);
    Check("R19_reject_executable_cache_section", state.KnownPresenceCacheAddresses.empty());
    sections[1].Characteristics &= ~IMAGE_SCN_MEM_EXECUTE;
    if (!VirtualProtect(memory + 0x3000, 0x1000, PAGE_NOACCESS, &previous)) return 2;
    DiscoverPresenceCaches(state, sections, 3);
    Check("R19_reject_unreadable_string", state.KnownPresenceCacheAddresses.empty());
    if (!VirtualProtect(memory + 0x3000, 0x1000, PAGE_READWRITE, &previous)) return 2;
    Check("R19_image_boundary", !IsImageRange(state, memory + 0x4000, 1) &&
        !IsImageRange(state, memory + 0x3FFF, 2) &&
        IsImageRange(state, memory + 0x3FFF, 1));

    if (!VirtualProtect(memory + 0x2000, 0x1000, PAGE_READONLY, &previous)) return 2;
    Check("R17_data_write_and_restore", WriteInt32WithProtection(cache, 2) && *cache == 2 &&
        Protection(cache) == PAGE_READONLY);
    Check("R17_identical_write", WriteInt32WithProtection(cache, 2) && Protection(cache) == PAGE_READONLY);
    state.KnownPresenceCacheAddresses.push_back(cache);
    Check("R19_apply_module_state_initial", ApplyModuleState(state, true) && *cache == kCachedPresent &&
        state.HasAppliedState && state.LastAppliedEnable);
    Check("R17_overwrite_cache_external", WriteInt32WithProtection(cache, kCachedNotPresent) && *cache == kCachedNotPresent);
    Check("R19_apply_module_state_cached_skip", ApplyModuleState(state, true) && *cache == kCachedNotPresent);
    state.HasAppliedState = false;
    Check("R19_refresh_reapplies_after_external_reset", ApplyModuleState(state, true) && *cache == kCachedPresent);
    state.KnownPresenceCacheAddresses.clear();
    std::atomic<bool> writesSucceeded{true};
    std::vector<std::thread> writers;
    for (int i = 0; i < 8; ++i) {
        writers.emplace_back([&, i] {
            for (int value = 3; value < 203; ++value) {
                if (!WriteInt32WithProtection(cache + (i & 1), value)) writesSucceeded = false;
            }
        });
    }
    for (auto& writer : writers) writer.join();
    Check("R17_same_page_concurrent_writes_restore", writesSucceeded && Protection(cache) == PAGE_READONLY);
    Check("R17_existing_executable_protection", WriteInt32WithProtection(reinterpret_cast<int*>(instruction), 7) &&
        Protection(instruction) == PAGE_EXECUTE_READ);
    if (!VirtualProtect(memory + 0x3000, 0x1000, PAGE_NOACCESS, &previous)) return 2;
    Check("R17_reject_noaccess", !WriteInt32WithProtection(reinterpret_cast<int*>(memory + 0x3000), 1));
    Check("R17_reject_cross_region", !WriteInt32WithProtection(reinterpret_cast<int*>(memory + 0x2FFF), 1));
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("SUMMARY hook failures=%d\n", failures);
    return failures ? 1 : 0;
}
