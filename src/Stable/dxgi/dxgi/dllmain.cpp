#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <atomic>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "User32.lib")

#pragma comment(linker, "/export:CreateDXGIFactory=C:\\Windows\\System32\\dxgi.CreateDXGIFactory")
#pragma comment(linker, "/export:CreateDXGIFactory1=C:\\Windows\\System32\\dxgi.CreateDXGIFactory1")
#pragma comment(linker, "/export:CreateDXGIFactory2=C:\\Windows\\System32\\dxgi.CreateDXGIFactory2")
#pragma comment(linker, "/export:DXGIDeclareAdapterRemovalSupport=C:\\Windows\\System32\\dxgi.DXGIDeclareAdapterRemovalSupport")
#pragma comment(linker, "/export:DXGIGetDebugInterface1=C:\\Windows\\System32\\dxgi.DXGIGetDebugInterface1")

using namespace winrt::Windows::Media::Control;

namespace Config {
    constexpr size_t QUEUE_BUFFER_SIZE = 1024;
    constexpr uintptr_t FS_COMMAND_OFFSETS[] = {
        0x764BC0,
        0x766910
    };
    constexpr int TARGET_STATION_ID = 7;
}

struct QueueEntry {
    char command[256];
    std::atomic<bool> ready{ false };
};

namespace State {
    QueueEntry cmdQueue[Config::QUEUE_BUFFER_SIZE];
    std::atomic<long> writeIndex{ 0 };
    std::atomic<long> readIndex{ 0 };
    std::atomic<long> currentStation{ -1 };
    std::atomic<long> lastStation{ -1 };
}

typedef void* (__fastcall* tFsCommand)(void* handler, void* movieView, const char* command, const char* args);
tFsCommand oFsCommand = nullptr;

void SendMediaKey(WORD vkey) {
    INPUT input[2] = {};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = vkey;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
}

DWORD WINAPI MediaControlThread(LPVOID lpReserved) {
    winrt::init_apartment();
    while (true) {
        try {
            auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            auto session = manager.GetCurrentSession();

            if (session) {
                long current = State::currentStation.load();
                auto status = session.GetPlaybackInfo().PlaybackStatus();

                if (current != State::lastStation.load()) {
                    if (current == Config::TARGET_STATION_ID) {
                        session.TryPlayAsync();
                    }
                    else if (current != -1) {
                        session.TryPauseAsync();
                    }
                    State::lastStation.store(current);
                }

                if (current != Config::TARGET_STATION_ID && current != -1) {
                    if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                        session.TryPauseAsync();
                    }
                }
            }
        }
        catch (...) {
        }
        Sleep(500);
    }
    return 0;
}

void* __fastcall hkFsCommand(void* handler, void* movieView, const char* command, const char* args) {
    if (command && (uintptr_t)command > 0x10000) {
        if (strstr(command, "STATION_UPDATE")) {
            for (int i = 0; command[i]; i++) {
                if (command[i] >= '0' && command[i] <= '9') {
                    State::currentStation.store(static_cast<long>(atoi(&command[i])));
                    break;
                }
            }
        }
        else if (strstr(command, "SP_")) {
            long idx = State::writeIndex.fetch_add(1) % Config::QUEUE_BUFFER_SIZE;
            strcpy_s(State::cmdQueue[idx].command, command);
            State::cmdQueue[idx].ready.store(true);
        }
    }
    return oFsCommand(handler, movieView, command, args);
}

int ApplyVTableHook(uintptr_t moduleBase, uintptr_t targetFn, PVOID hookFn) {
    int hookCount = 0;
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)moduleBase;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(moduleBase + dosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (memcmp(section->Name, ".rdata", 6) == 0 || memcmp(section->Name, ".data", 5) == 0) {
            uintptr_t start = moduleBase + section->VirtualAddress;
            uintptr_t end = start + section->Misc.VirtualSize;

            for (uintptr_t p = start; p < end - sizeof(uintptr_t); p += sizeof(uintptr_t)) {
                uintptr_t* ptr = reinterpret_cast<uintptr_t*>(p);
                if (*ptr == targetFn) {
                    DWORD oldProtect;
                    if (VirtualProtect(ptr, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
                        *ptr = reinterpret_cast<uintptr_t>(hookFn);
                        VirtualProtect(ptr, sizeof(uintptr_t), oldProtect, &oldProtect);
                        hookCount++;
                    }
                }
            }
        }
    }
    return hookCount;
}

DWORD WINAPI EventProcessorThread(LPVOID lpReserved) {
    while (true) {
        while (State::readIndex.load() < State::writeIndex.load()) {
            long idx = State::readIndex.load() % Config::QUEUE_BUFFER_SIZE;

            if (State::cmdQueue[idx].ready.exchange(false)) {
                const char* found = strstr(State::cmdQueue[idx].command, "SP_");
                if (found && State::currentStation.load() == Config::TARGET_STATION_ID) {
                    char action = found[3];
                    switch (action) {
                    case '0': SendMediaKey(VK_MEDIA_PREV_TRACK); break;
                    case '1': SendMediaKey(VK_MEDIA_PLAY_PAUSE); break;
                    case '3': SendMediaKey(VK_MEDIA_NEXT_TRACK); break;
                    }
                }
                State::readIndex.fetch_add(1);
            }
            else {
                break;
            }
        }
        Sleep(10);
    }
    return 0;
}

DWORD WINAPI MainThread(LPVOID lpReserved) {
    uintptr_t moduleBase = 0;
    while ((moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL))) == 0) {
        Sleep(100);
    }

    Sleep(3000);

    int fsHooks = 0;
    for (uintptr_t offset : Config::FS_COMMAND_OFFSETS) {
        oFsCommand = reinterpret_cast<tFsCommand>(moduleBase + offset);
        fsHooks = ApplyVTableHook(moduleBase, reinterpret_cast<uintptr_t>(oFsCommand), hkFsCommand);
        if (fsHooks > 0) {
            break;
        }
    }

    if (fsHooks > 0) {
        CreateThread(nullptr, 0, EventProcessorThread, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, MediaControlThread, nullptr, 0, nullptr);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE hThread = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
