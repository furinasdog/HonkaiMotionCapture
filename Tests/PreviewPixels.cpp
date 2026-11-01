// SPDX-License-Identifier: MIT
#include "PreviewPixels.h"
#include <iostream>
#include <limits>

int main()
{
    const uint8_t rgb[] = {255, 0, 0, 0, 255, 0, 17, 19, 0, 0, 255, 12, 34, 56};
    std::vector<uint8_t> bgra;
    if (!HonkaiCaptureNative::CopyPreviewRgbToBgra(rgb, 2, 2, 8, sizeof(rgb), bgra)) return 1;
    const std::vector<uint8_t> expected{0,0,255,255, 0,255,0,255, 255,0,0,255, 56,34,12,255};
    if (bgra != expected) return 2;
    for (const auto invalid : {0, -1, 321})
        if (HonkaiCaptureNative::CopyPreviewRgbToBgra(rgb, invalid, 2, 8, sizeof(rgb), bgra) || !bgra.empty()) return 3;
    if (HonkaiCaptureNative::CopyPreviewRgbToBgra(rgb, 2, 241, 8, sizeof(rgb), bgra)
        || HonkaiCaptureNative::CopyPreviewRgbToBgra(rgb, 2, 2, 5, sizeof(rgb), bgra)
        || HonkaiCaptureNative::CopyPreviewRgbToBgra(rgb, 2, 2, 8, sizeof(rgb)-1, bgra)
        || HonkaiCaptureNative::CopyPreviewRgbToBgra(rgb, 2, 2, std::numeric_limits<size_t>::max(), sizeof(rgb), bgra)
        || HonkaiCaptureNative::CopyPreviewRgbToBgra(nullptr, 2, 2, 8, sizeof(rgb), bgra)) return 4;
    std::vector<uint8_t> full(320 * 240 * 3, 42);
    if (!HonkaiCaptureNative::CopyPreviewRgbToBgra(full.data(), 320, 240, 960, full.size(), bgra)
        || bgra.size() != 320 * 240 * 4 || bgra.back() != 255) return 5;
    std::cout << "PASS preview RGB/BGRA pixels, padded stride, short buffers, overflow and 320x240 bound; no camera\n";
    return 0;
}
