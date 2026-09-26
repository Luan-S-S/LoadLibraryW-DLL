// dllmain.cpp : Define o ponto de entrada para o aplicativo DLL.
#include "pch.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <string>

LPVOID WriteData(HANDLE hProcess, LPVOID Pointer, SIZE_T size, DWORD flProtect)
{
    LPVOID lpBaseAddress = VirtualAllocEx(hProcess, nullptr, size, MEM_COMMIT | MEM_RESERVE, flProtect);
    SIZE_T lpNumberOfBytesWritten;
    WriteProcessMemory(hProcess, lpBaseAddress, Pointer, size, &lpNumberOfBytesWritten);
    return lpBaseAddress;
}

LPCWSTR GetPathNameW(LPCWSTR wPath)
{
    LPCWSTR name = wcsrchr(wPath, L'\\');
    return name ? name + 1 : nullptr;
}

HMODULE GetRemoteModule(const HANDLE hProcess, LPCWSTR modulePath)
{
    DWORD pid = GetProcessId(hProcess);
    LPCWSTR moduleName = GetPathNameW(modulePath);

    if (pid == 0 || !moduleName) return nullptr;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE) return nullptr;

    HMODULE module = nullptr;

    MODULEENTRY32W mod;
    mod.dwSize = sizeof(mod);

    if (Module32FirstW(hSnapshot, &mod)) {
        do {
            if (_wcsicmp(mod.szModule, moduleName) == 0) {
                module = mod.hModule;
                break;
            }
        } while (Module32NextW(hSnapshot, &mod));
    }

    CloseHandle(hSnapshot);
    return module;
}

constexpr DWORD ERROR_REMOTE_PROCESS_WRITE_PATHDLL_FAILED = 0xA; //Erro ao escrever pathDll  no processo
constexpr DWORD ERROR_FUNCTION_ADDRESS_NOT_FOUND = 0xE; // Erro ao encontrar endereço da função
constexpr DWORD ERROR_REMOTE_THREAD_CREATE_FAILED = 0xF; // Erro ao criar Thread remota
constexpr DWORD ERROR_MEMORY_RELEASE_FAILED = 0x10;
constexpr DWORD ERROR_GET_EXIT_CODE_FAILED = 0x11;
constexpr DWORD ERROR_LOADLIBRARY_FAILED = 0x12;
constexpr DWORD ERROR_REMOTE_MODULE_NOT_FOUND = 0x13;

extern "C" __declspec(dllexport)
HMODULE __stdcall LoadDllW(HANDLE hProcess, const wchar_t* pathDll, DWORD* errorCode)
{
    if (!errorCode) return NULL;

    if (!hProcess) {
        *errorCode = ERROR_INVALID_HANDLE;
        return NULL;
    }
    if (!pathDll) {
        *errorCode = ERROR_INVALID_PARAMETER;
        return NULL;
    }

    *errorCode = 0;

    LPTHREAD_START_ROUTINE ptrLoadW = nullptr;
    HANDLE hThread = nullptr;
    HMODULE module = nullptr;
    DWORD exitCode = 0;


    LPVOID ptrPath = WriteData(hProcess, (LPVOID) pathDll, wcslen(pathDll) * sizeof(WCHAR) + sizeof(WCHAR), PAGE_READWRITE);

    if (!ptrPath) { *errorCode = ERROR_REMOTE_PROCESS_WRITE_PATHDLL_FAILED; goto release; }

    ptrLoadW = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");

    if (!ptrLoadW) { *errorCode = ERROR_FUNCTION_ADDRESS_NOT_FOUND; goto release; }

    hThread = CreateRemoteThread(hProcess, nullptr, 0, ptrLoadW, ptrPath, 0, nullptr);

    if (!hThread) { *errorCode = ERROR_REMOTE_THREAD_CREATE_FAILED; goto release; }

    WaitForSingleObject(hThread, INFINITE);

    if (!GetExitCodeThread(hThread, &exitCode)) { *errorCode = ERROR_GET_EXIT_CODE_FAILED; goto release; }

    if (exitCode == 0) { *errorCode = ERROR_LOADLIBRARY_FAILED; goto release; }

    module = GetRemoteModule(hProcess, pathDll);
    if (!module) *errorCode = ERROR_REMOTE_MODULE_NOT_FOUND;

release:

    if (ptrPath) {
        if (!VirtualFreeEx(hProcess, ptrPath, 0, MEM_RELEASE) && *errorCode == 0) *errorCode = ERROR_MEMORY_RELEASE_FAILED;  
    }
    
    if(hThread) CloseHandle(hThread);

    return module;
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

