// SPDX-License-Identifier: MIT
#pragma once

namespace HonkaiCaptureNative
{
// Read-only probe. OpenCVHelper owns the runtime reference until capture workers have stopped.
bool IsOpenCvRuntimeLoaded(const wchar_t* runtimePath) noexcept;
}
