#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "User32.lib")

using namespace winrt;
using namespace Windows::Media::Control;

// --- AUTO-LOADER PROXY ---
#pragma comment(linker, "/export:CreateDXGIFactory=C:\\Windows\\System32\\dxgi.CreateDXGIFactory")
#pragma comment(linker, "/export:CreateDXGIFactory1=C:\\Windows\\System32\\dxgi.CreateDXGIFactory1")
#pragma comment(linker, "/export:CreateDXGIFactory2=C:\\Windows\\System32\\dxgi.CreateDXGIFactory2")
#pragma comment(linker, "/export:DXGIDeclareAdapterRemovalSupport=C:\\Windows\\System32\\dxgi.DXGIDeclareAdapterRemovalSupport")
#pragma comment(linker, "/export:DXGIGetDebugInterface1=C:\\Windows\\System32\\dxgi.DXGIGetDebugInterface1")

// ============================================================================
// GLOBALS
// ============================================================================
#define LOG_BUFFER_SIZE 1024
struct LogEntry { char msg[256]; volatile LONG ready; };
LogEntry g_LogQueue[LOG_BUFFER_SIZE];
volatile LONG g_WriteIndex = 0;
volatile LONG g_ReadIndex = 0;

// Shared strings for the hook to read
char g_Artist[256] = "Waiting...";
char g_Title[256] = "No Media";

typedef void* (__fastcall* tGFxInvoke)(void* pThis, const char* methodName, void* pResult, void* pArgs, int numArgs);
tGFxInvoke oGFxInvoke = nullptr;
typedef void* (__fastcall* tFsCommand)(void* handler, void* movieView, const char* command, const char* args);
tFsCommand oFsCommand = nullptr;

BYTE g_OrigBytes[14], g_JmpBytes[14];
volatile LONG g_HookLock = 0;

// ============================================================================
// WINDOWS MEDIA SESSION API (The "Black Box" Logic)
// ============================================================================

DWORD WINAPI MediaThread(LPVOID) {
    // Initialize WinRT Apartment
    winrt::init_apartment();

    while (true) {
        try {
            // Get the session manager (same one Windows uses for the overlay)
            auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            auto session = manager.GetCurrentSession();

            if (session) {
                // Get the current song properties
                auto props = session.TryGetMediaPropertiesAsync().get();

                std::string artist = winrt::to_string(props.Artist());
                std::string title = winrt::to_string(props.Title());

                if (!artist.empty()) strcpy_s(g_Artist, artist.c_str());
                if (!title.empty()) strcpy_s(g_Title, title.c_str());
            }
            else {
                strcpy_s(g_Artist, "Spotify");
                strcpy_s(g_Title, "No Media Playing");
            }
        }
        catch (...) {
            // Silently handle cases where no media session exists
        }
        Sleep(1000);
    }
    return 0;
}

// ============================================================================
// HOOK IMPLEMENTATIONS (Stable Spin-Lock)
// ============================================================================

void* __fastcall hkGFxInvoke(void* pThis, const char* methodName, void* pResult, void* pArgs, int numArgs)
{
    while (InterlockedExchange(&g_HookLock, 1) == 1) { _mm_pause(); }
    memcpy(oGFxInvoke, g_OrigBytes, 14);

    if (methodName && (uintptr_t)methodName > 0x10000)
    {
        if (pArgs && strstr(methodName, "setTrackInfos"))
        {
            unsigned char* pData = (unsigned char*)pArgs;
            *(const char**)(pData + 0x10) = g_Artist; // Injected from WinRT
            *(const char**)(pData + 0x30) = g_Title;  // Injected from WinRT
        }

        LONG index = InterlockedExchangeAdd(&g_WriteIndex, 1) % LOG_BUFFER_SIZE;
        strcpy_s(g_LogQueue[index].msg, "[INVOKE] ");
        strncat_s(g_LogQueue[index].msg, methodName, 100);
        InterlockedExchange(&g_LogQueue[index].ready, 1);
    }

    void* res = oGFxInvoke(pThis, methodName, pResult, pArgs, numArgs);
    memcpy(oGFxInvoke, g_JmpBytes, 14);
    InterlockedExchange(&g_HookLock, 0);
    return res;
}

void* __fastcall hkFsCommand(void* handler, void* movieView, const char* command, const char* args)
{
    if (command && (uintptr_t)command > 0x10000)
    {
        LONG index = InterlockedExchangeAdd(&g_WriteIndex, 1) % LOG_BUFFER_SIZE;
        strcpy_s(g_LogQueue[index].msg, "[FSCOMMAND] ");
        strncat_s(g_LogQueue[index].msg, command, 100);
        InterlockedExchange(&g_LogQueue[index].ready, 1);
    }
    return oFsCommand(handler, movieView, command, args);
}

// ============================================================================
// STABLE VMT SCANNER
// ============================================================================

void ReplaceVTablePointers(uintptr_t base, uintptr_t target, PVOID hook) {
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + ((PIMAGE_DOS_HEADER)base)->e_lfanew);
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (memcmp(sec->Name, ".rdata", 6) == 0 || memcmp(sec->Name, ".data", 5) == 0) {
            uintptr_t start = base + sec->VirtualAddress;
            uintptr_t end = start + sec->Misc.VirtualSize;
            for (uintptr_t p = start; p < end - 8; p += 8) {
                if (*(uintptr_t*)p == target) {
                    DWORD old; VirtualProtect((void*)p, 8, PAGE_READWRITE, &old);
                    *(uintptr_t*)p = (uintptr_t)hook;
                    VirtualProtect((void*)p, 8, old, &old);
                }
            }
        }
    }
}

// ============================================================================
// MEDIA KEY LOGGER THREAD
// ============================================================================

DWORD WINAPI LoggerThread(LPVOID) {
    while (true) {
        while (g_ReadIndex < g_WriteIndex) {
            LONG index = g_ReadIndex % LOG_BUFFER_SIZE;
            if (InterlockedCompareExchange(&g_LogQueue[index].ready, 0, 1) == 1) {
                const char* found = strstr(g_LogQueue[index].msg, "SP_");
                if (found) {
                    char d = found[3];
                    BYTE k = (d == '0') ? VK_MEDIA_PREV_TRACK : (d == '1') ? VK_MEDIA_PLAY_PAUSE : (d == '3') ? VK_MEDIA_NEXT_TRACK : 0;
                    if (k) { keybd_event(k, 0, 0, 0); keybd_event(k, 0, KEYEVENTF_KEYUP, 0); }
                }
                g_ReadIndex++;
            }
            else break;
        }
        Sleep(5);
    }
    return 0;
}

// ============================================================================
// MAIN INITIALIZATION
// ============================================================================

DWORD WINAPI MainThread(LPVOID) {
    Sleep(10000);
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!base) return 0;

    oGFxInvoke = (tGFxInvoke)(base + 0x76E160);
    oFsCommand = (tFsCommand)(base + 0x766910);

    // Setup Inline Hook for Invoke (Artist injection)
    DWORD old; VirtualProtect(oGFxInvoke, 14, PAGE_EXECUTE_READWRITE, &old);
    g_JmpBytes[0] = 0xFF; g_JmpBytes[1] = 0x25; *(DWORD*)(&g_JmpBytes[2]) = 0;
    *(uintptr_t*)(&g_JmpBytes[6]) = (uintptr_t)hkGFxInvoke;
    memcpy(g_OrigBytes, oGFxInvoke, 14);
    memcpy(oGFxInvoke, g_JmpBytes, 14);

    // Setup VMT Hooks for Buttons (Pause/Skip/Back)
    ReplaceVTablePointers(base, (uintptr_t)oGFxInvoke, hkGFxInvoke);
    ReplaceVTablePointers(base, (uintptr_t)oFsCommand, hkFsCommand);

    CreateThread(nullptr, 0, LoggerThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, MediaThread, nullptr, 0, nullptr);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID res) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}