#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#pragma comment(lib, "User32.lib")

#pragma comment(linker, "/export:CreateDXGIFactory=C:\\Windows\\System32\\dxgi.CreateDXGIFactory")
#pragma comment(linker, "/export:CreateDXGIFactory1=C:\\Windows\\System32\\dxgi.CreateDXGIFactory1")
#pragma comment(linker, "/export:CreateDXGIFactory2=C:\\Windows\\System32\\dxgi.CreateDXGIFactory2")
#pragma comment(linker, "/export:DXGIDeclareAdapterRemovalSupport=C:\\Windows\\System32\\dxgi.DXGIDeclareAdapterRemovalSupport")
#pragma comment(linker, "/export:DXGIGetDebugInterface1=C:\\Windows\\System32\\dxgi.DXGIGetDebugInterface1")

#define LOG_BUFFER_SIZE 1024
struct LogEntry { char msg[256]; volatile LONG ready; };
LogEntry g_LogQueue[LOG_BUFFER_SIZE];
volatile LONG g_WriteIndex = 0;
volatile LONG g_ReadIndex = 0;

typedef void* (__fastcall* tFsCommand)(void* handler, void* movieView, const char* command, const char* args);
tFsCommand oFsCommand = nullptr;

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

DWORD WINAPI MainThread(LPVOID) {
    Sleep(10000);
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!base) return 0;

    oFsCommand = (tFsCommand)(base + 0x766910);

    ReplaceVTablePointers(base, (uintptr_t)oFsCommand, hkFsCommand);

    CreateThread(nullptr, 0, LoggerThread, nullptr, 0, nullptr);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID res) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
