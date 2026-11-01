// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace HonkaiCaptureNative
{
// Input may contain padded RGB rows; output always has width*4 bytes per row.
bool CopyPreviewRgbToBgra(const uint8_t* rgb, int width, int height, size_t stride,
    size_t readableBytes, std::vector<uint8_t>& bgra);
}
