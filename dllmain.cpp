#include <Windows.h>
#include <xaudio2.h>
#include "MinHook.h"
#include <unordered_set>
#include <unordered_map>
#include <cstdio>
#include <d3d11.h>
#include <string>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3d11.lib")

std::unordered_set<std::string> blacklistedBaseNames;

// ---------------------------
// XAudio2 PTRs
// ---------------------------
typedef HRESULT(WINAPI* XAudio2Create_t)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
typedef HRESULT(WINAPI* XAudio2CreateWithDebug_t)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
typedef HRESULT(WINAPI* CreateSourceVoice_t)(
    IXAudio2*, IXAudio2SourceVoice**, const WAVEFORMATEX*, UINT32, float,
    IXAudio2VoiceCallback*, const XAUDIO2_VOICE_SENDS*, const XAUDIO2_EFFECT_CHAIN*);
typedef HRESULT(WINAPI* Start_t)(IXAudio2SourceVoice*, UINT32, UINT32);
typedef HRESULT(WINAPI* Stop_t)(IXAudio2SourceVoice*, UINT32, UINT32);
typedef HRESULT(WINAPI* Submit_t)(IXAudio2SourceVoice*, const XAUDIO2_BUFFER*, const XAUDIO2_BUFFER_WMA*);
typedef HRESULT(WINAPI* Flush_t)(IXAudio2SourceVoice*);

// ---------------------------
// D3D11 PTRs (FIXED SIGNATURES)
// ---------------------------
typedef HRESULT(WINAPI* D3D11CreateDevice_t)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

typedef HRESULT(WINAPI* D3D11CreateDeviceAndSwapChain_t)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*,
    IDXGISwapChain**, ID3D11Device**,
    D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

typedef HRESULT(WINAPI* CreateSamplerState_t)(
    ID3D11Device*, const D3D11_SAMPLER_DESC*, ID3D11SamplerState**);

// ---------------------------
// ORIGINAL FUNCTION PTRS
// ---------------------------
D3D11CreateDevice_t origD3D11CreateDevice = nullptr;
D3D11CreateDeviceAndSwapChain_t origCreateDeviceSwap = nullptr;
CreateSamplerState_t origCreateSamplerState = nullptr;

XAudio2Create_t origXAudio2Create = nullptr;
CreateSourceVoice_t origCreateSourceVoice = nullptr;
Start_t origStart = nullptr;
Stop_t origStop = nullptr;
Submit_t origSubmit = nullptr;
Flush_t origFlush = nullptr;

// ---------------------------
// XAudio2 STATE
// ---------------------------
struct VoiceState { XAUDIO2_BUFFER lastBuffer{}; bool hasBuffer = false; };
std::unordered_map<IXAudio2SourceVoice*, VoiceState> g_VoiceStates;

// ---------------------------
// UTIL
// ---------------------------
std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    return std::wstring(path);
}

void LoadShaderBlacklistFromConfig() {
    blacklistedBaseNames.clear();

    std::wstring exeDirW = GetExeDirectory();
    std::string exeDir(exeDirW.begin(), exeDirW.end());
    std::string cfgPath = exeDir + "\\fan_patches.cfg";

    std::ifstream file(cfgPath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == '#') continue;
        blacklistedBaseNames.insert(line);
    }
}

void PatchShaderNameTables() {
    uintptr_t moduleBase = (uintptr_t)GetModuleHandleW(NULL);
    char** pixelTable = reinterpret_cast<char**>(moduleBase + 0x16d3c40);

    static const char* replacementPixel = "generic_p";

    DWORD old;
    VirtualProtect(pixelTable, 12 * sizeof(char*), PAGE_READWRITE, &old);

    for (int i = 0; i < 12; ++i) {
        if (pixelTable[i]) {
            std::string name(pixelTable[i]);
            if (blacklistedBaseNames.count(name)) {
                pixelTable[i] = const_cast<char*>(replacementPixel);
            }
        }
    }

    VirtualProtect(pixelTable, 12 * sizeof(char*), old, &old);
}

// ---------------------------
// D3D11 HOOKS
// ---------------------------
HRESULT WINAPI hkCreateSamplerState(ID3D11Device* device, const D3D11_SAMPLER_DESC* desc, ID3D11SamplerState** state)
{
    if (!desc) return origCreateSamplerState(device, desc, state);

    D3D11_SAMPLER_DESC mod = *desc;
    mod.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    mod.MaxAnisotropy = 1;
    return origCreateSamplerState(device, &mod, state);
}

void HkD3DDevice(ID3D11Device* device)
{
    if (!device) return;

    void** vtable = *(void***)device;
    MH_CreateHook(vtable[23], hkCreateSamplerState, (LPVOID*)&origCreateSamplerState);
    MH_EnableHook(vtable[23]);
}

HRESULT WINAPI hkD3D11CreateDevice(
    IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE driver,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL* levels,
    UINT levelCount,
    UINT sdk,
    ID3D11Device** device,
    D3D_FEATURE_LEVEL* feature,
    ID3D11DeviceContext** context)
{
    HRESULT hr = origD3D11CreateDevice(adapter, driver, software, flags,
        levels, levelCount, sdk, device, feature, context);

    if (SUCCEEDED(hr) && device && *device)
        HkD3DDevice(*device);

    return hr;
}

HRESULT WINAPI hkD3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE driver,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL* levels,
    UINT levelCount,
    UINT sdk,
    const DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** swap,
    ID3D11Device** device,
    D3D_FEATURE_LEVEL* feature,
    ID3D11DeviceContext** context)
{
    HRESULT hr = origCreateDeviceSwap(adapter, driver, software, flags,
        levels, levelCount, sdk, desc, swap, device, feature, context);

    if (SUCCEEDED(hr) && device && *device)
        HkD3DDevice(*device);

    return hr;
}

// ---------------------------
// XAudio2 HOOKS
// ---------------------------
HRESULT WINAPI hkStart(IXAudio2SourceVoice* voice, UINT32 f, UINT32 s)
{
    return origStart(voice, f, s);
}

HRESULT WINAPI hkFlush(IXAudio2SourceVoice* voice)
{
    return origFlush(voice);
}

HRESULT WINAPI hkStop(IXAudio2SourceVoice* voice, UINT32 f, UINT32 s)
{
    if (!voice) return S_OK;

    origStop(voice, XAUDIO2_COMMIT_NOW, 0);
    hkFlush(voice);

    auto it = g_VoiceStates.find(voice);
    if (it != g_VoiceStates.end() && it->second.hasBuffer)
    {
        XAUDIO2_BUFFER buf = it->second.lastBuffer;
        buf.Flags |= XAUDIO2_END_OF_STREAM;
        origSubmit(voice, &buf, nullptr);
    }
    return S_OK;
}

HRESULT WINAPI hkSubmit(IXAudio2SourceVoice* voice, const XAUDIO2_BUFFER* buf, const XAUDIO2_BUFFER_WMA* wma)
{
    if (voice && buf)
    {
        VoiceState& state = g_VoiceStates[voice];
        state.lastBuffer = *buf;
        state.hasBuffer = true;
    }
    return origSubmit(voice, buf, wma);
}

void HookVoice(IXAudio2SourceVoice* voice)
{
    if (!voice) return;

    void** vtable = *(void***)voice;

    MH_CreateHook(vtable[19], hkStart, (LPVOID*)&origStart);
    MH_CreateHook(vtable[20], hkStop, (LPVOID*)&origStop);
    MH_CreateHook(vtable[21], hkSubmit, (LPVOID*)&origSubmit);
    MH_CreateHook(vtable[22], hkFlush, (LPVOID*)&origFlush);

    MH_EnableHook(vtable[19]);
    MH_EnableHook(vtable[20]);
    MH_EnableHook(vtable[21]);
    MH_EnableHook(vtable[22]);

    g_VoiceStates[voice] = {};
}

HRESULT WINAPI hkCreateSourceVoice(
    IXAudio2* This,
    IXAudio2SourceVoice** ppVoice,
    const WAVEFORMATEX* fmt,
    UINT32 flags,
    float ratio,
    IXAudio2VoiceCallback* cb,
    const XAUDIO2_VOICE_SENDS* sends,
    const XAUDIO2_EFFECT_CHAIN* fx)
{
    HRESULT hr = origCreateSourceVoice(This, ppVoice, fmt, flags, ratio, cb, sends, fx);

    if (SUCCEEDED(hr) && ppVoice && *ppVoice)
        HookVoice(*ppVoice);

    return hr;
}

HRESULT WINAPI hkXAudio2Create(IXAudio2** ppXAudio2, UINT32 flags, XAUDIO2_PROCESSOR proc)
{
    HRESULT hr = origXAudio2Create(ppXAudio2, flags, proc);

    if (SUCCEEDED(hr) && ppXAudio2 && *ppXAudio2)
    {
        void** vtable = *(void***)*ppXAudio2;
        MH_CreateHook(vtable[5], hkCreateSourceVoice, (LPVOID*)&origCreateSourceVoice);
        MH_EnableHook(vtable[5]);
    }

    return hr;
}

// ---------------------------
// INIT
// ---------------------------
DWORD WINAPI InitThread(LPVOID)
{
    MH_Initialize();

    // XAudio2
    HMODULE xaudio = nullptr;
    while (!xaudio) { xaudio = GetModuleHandleA("xaudio2_9redist.dll"); Sleep(50); }

    void* addr = GetProcAddress(xaudio, "XAudio2Create");
    MH_CreateHook(addr, hkXAudio2Create, (LPVOID*)&origXAudio2Create);
    MH_EnableHook(addr);

    // D3D11
    HMODULE d3d = nullptr;
    while (!d3d) { d3d = GetModuleHandleA("d3d11.dll"); Sleep(50); }

    void* addr1 = GetProcAddress(d3d, "D3D11CreateDevice");
    void* addr2 = GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain");

    MH_CreateHook(addr1, hkD3D11CreateDevice, (LPVOID*)&origD3D11CreateDevice);
    MH_EnableHook(addr1);

    MH_CreateHook(addr2, hkD3D11CreateDeviceAndSwapChain, (LPVOID*)&origCreateDeviceSwap);
    MH_EnableHook(addr2);

    return 0;
}

// ---------------------------
// DLL MAIN
// ---------------------------
BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);

        LoadShaderBlacklistFromConfig();
        PatchShaderNameTables();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        MH_Uninitialize();
    }
    return TRUE;
}
