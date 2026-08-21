#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#pragma comment(lib, "windowsapp")
#pragma comment(linker, "/export:CreateDXGIFactory=C:\\Windows\\System32\\dxgi.CreateDXGIFactory")
#pragma comment(linker, "/export:CreateDXGIFactory1=C:\\Windows\\System32\\dxgi.CreateDXGIFactory1")
#pragma comment(linker, "/export:CreateDXGIFactory2=C:\\Windows\\System32\\dxgi.CreateDXGIFactory2")
#pragma comment(linker, "/export:DXGIDeclareAdapterRemovalSupport=C:\\Windows\\System32\\dxgi.DXGIDeclareAdapterRemovalSupport")
#pragma comment(linker, "/export:DXGIGetDebugInterface1=C:\\Windows\\System32\\dxgi.DXGIGetDebugInterface1")

using namespace winrt::Windows::Media::Control;

constexpr int TARGET_STATION_ID = 7;
constexpr uintptr_t FS_COMMAND_OFFSETS[] = { 0x764BC0, 0x766910 };

using tFsCommand = void* (__fastcall*)(void* handler, void* movieView, const char* command, const char* args);
tFsCommand oFsCommand = nullptr;

static GlobalSystemMediaTransportControlsSessionManager g_mediaManager{ nullptr };
static int g_currentStation = -1;

void HandleStationChange(int newStation) {
    if (newStation == g_currentStation) return;

    if (g_mediaManager) {
        try {
            if (auto session = g_mediaManager.GetCurrentSession()) {
                if (newStation == TARGET_STATION_ID) {
                    session.TryPlayAsync();
                } else if (g_currentStation == TARGET_STATION_ID) {
                    session.TryPauseAsync();
                }
            }
        } catch (...) {}
    }
    g_currentStation = newStation;
}

void HandleMediaControl(char action) {
    if (!g_mediaManager || g_currentStation != TARGET_STATION_ID) return;

    try {
        if (auto session = g_mediaManager.GetCurrentSession()) {
            switch (action) {
                case '0': session.TrySkipPreviousAsync();    break;
                case '1': session.TryTogglePlayPauseAsync(); break;
                case '3': session.TrySkipNextAsync();        break;
            }
        }
    } catch (...) {}
}

void* __fastcall hkFsCommand(void* handler, void* movieView, const char* command, const char* args) {
    if (command && (uintptr_t)command > 0x10000) {
        if (strstr(command, "STATION_UPDATE")) {
            const char* numPtr = (args && *args) ? args : strpbrk(command, "0123456789");
            if (numPtr) {
                HandleStationChange(atoi(numPtr));
            }
        } 
        else if (const char* sp = strstr(command, "SP_")) {
            HandleMediaControl(sp[3]);
        }
    }
    return oFsCommand(handler, movieView, command, args);
}

bool ApplyVTableHook(uintptr_t moduleBase, uintptr_t targetFn, void* hookFn) {
    auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(ntHeaders);

    bool hooked = false;
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (memcmp(section->Name, ".rdata", 6) != 0 && memcmp(section->Name, ".data", 5) != 0)
            continue;

        auto* start = reinterpret_cast<uintptr_t*>(moduleBase + section->VirtualAddress);
        auto* end = reinterpret_cast<uintptr_t*>(moduleBase + section->VirtualAddress + section->Misc.VirtualSize);

        for (uintptr_t* ptr = start; ptr < end; ++ptr) {
            if (*ptr == targetFn) {
                DWORD oldProtect;
                if (VirtualProtect(ptr, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
                    *ptr = reinterpret_cast<uintptr_t>(hookFn);
                    VirtualProtect(ptr, sizeof(uintptr_t), oldProtect, &oldProtect);
                    hooked = true;
                }
            }
        }
    }
    return hooked;
}

DWORD WINAPI InitThread(LPVOID) {
    uintptr_t moduleBase = 0;
    while (!(moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))) {
        Sleep(100);
    }

    try {
        winrt::init_apartment();
        g_mediaManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (...) {}

    for (uintptr_t offset : FS_COMMAND_OFFSETS) {
        oFsCommand = reinterpret_cast<tFsCommand>(moduleBase + offset);
        if (ApplyVTableHook(moduleBase, reinterpret_cast<uintptr_t>(oFsCommand), reinterpret_cast<void*>(hkFsCommand))) {
            break;
        }
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE hThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
