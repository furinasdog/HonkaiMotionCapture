// SPDX-License-Identifier: MIT
#include "OpenCvRuntimeCheck.h"

#if defined(HONKAI_CAPTURE_UNREAL) && HONKAI_CAPTURE_UNREAL
#include "Windows/WindowsHWrapper.h"
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

bool HonkaiCaptureNative::IsOpenCvRuntimeLoaded(const wchar_t* runtimePath) noexcept
{
    return runtimePath && *runtimePath && ::GetModuleHandleW(runtimePath) != nullptr;
}
