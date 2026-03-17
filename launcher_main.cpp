#include <Windows.h>
#include <string>

int wmain()
{
    const wchar_t* exePath = L"FFVII.exe";
    const wchar_t* dllPath = L"ff7_fanfix.dll";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exePath, nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
        return GetLastError();

    // Allocate memory in the target process
    size_t len = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remoteStr = VirtualAllocEx(pi.hProcess, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(pi.hProcess, remoteStr, dllPath, len, nullptr);

    // Get LoadLibraryW address
    LPTHREAD_START_ROUTINE loadLibAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW");

    // Create remote thread to load your DLL
    HANDLE hThread = CreateRemoteThread(pi.hProcess, nullptr, 0, loadLibAddr, remoteStr, 0, nullptr);
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFreeEx(pi.hProcess, remoteStr, 0, MEM_RELEASE);

    // Resume FFVII
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return 0;
}