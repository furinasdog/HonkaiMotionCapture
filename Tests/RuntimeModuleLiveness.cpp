// SPDX-License-Identifier: MIT
#include "OpenCvRuntimeCheck.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
struct OwnedModule
{
    HMODULE handle = nullptr;
    ~OwnedModule() { Reset(); }
    void Reset() { if (handle) { FreeLibrary(handle); handle = nullptr; } }
};
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void ProbeRepeatedly(const wchar_t* path, HMODULE owner, FARPROC constructor)
{
    for (int i = 0; i < 1000; ++i)
    {
        Require(HonkaiCaptureNative::IsOpenCvRuntimeLoaded(path), "Read-only preflight lost the loaded runtime");
        HMODULE observed = nullptr;
        // Query the actual constructor code mapping without adding a reference. Never execute it.
        Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(constructor), &observed) != 0 && observed == owner,
            "Preflight unloaded the runtime behind the constructor address");
    }
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: RuntimeModuleLiveness <installed OpenCV runtime DLL>");
        const auto dll = std::filesystem::absolute(argv[1]);
        const auto uePath = dll.generic_wstring();
        Require(!HonkaiCaptureNative::IsOpenCvRuntimeLoaded(nullptr), "Null path must not match the executable module");
        Require(!HonkaiCaptureNative::IsOpenCvRuntimeLoaded(L""), "Empty path must fail");
        Require(!HonkaiCaptureNative::IsOpenCvRuntimeLoaded(L"HonkaiMissingOpenCvRuntime.dll"), "Missing module must fail");
        Require(!HonkaiCaptureNative::IsOpenCvRuntimeLoaded(dll.c_str()), "Test process must start without OpenCV loaded");
        for (int round = 0; round < 3; ++round)
        {
            OwnedModule helperOwner;
            helperOwner.handle = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Require(helperOwner.handle != nullptr, "Cannot load installed OpenCV runtime for the ownership test");
            const FARPROC constructor = GetProcAddress(helperOwner.handle, "??0VideoCapture@cv@@QEAA@XZ");
            Require(constructor != nullptr, "Expected VideoCapture constructor export is absent");
            ProbeRepeatedly(uePath.c_str(), helperOwner.handle, constructor);
            OwnedModule independentOwner;
            // Flags0 obtains one real reference; do not PIN the module.
            Require(GetModuleHandleExW(0, dll.c_str(), &independentOwner.handle) != 0, "Cannot acquire independent owner reference");
            helperOwner.Reset();
            ProbeRepeatedly(uePath.c_str(), independentOwner.handle, constructor);
            independentOwner.Reset();
            Require(!HonkaiCaptureNative::IsOpenCvRuntimeLoaded(dll.c_str()), "Read-only preflight added a reference or pinned the runtime");
            std::cout << "round=" << round << " probes=2000 live_until_last_owner=1 unloaded_after_last_owner=1\n";
        }
        std::cout << "PASS actual OpenCV module liveness and unchanged ownership; no camera/SDK/UE calls\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
