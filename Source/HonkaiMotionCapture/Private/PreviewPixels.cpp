// SPDX-License-Identifier: MIT
#include "PreviewPixels.h"
#include <limits>

bool HonkaiCaptureNative::CopyPreviewRgbToBgra(const uint8_t* rgb, int width, int height,
    size_t stride, size_t readableBytes, std::vector<uint8_t>& bgra)
{
    bgra.clear();
    if (!rgb || width < 1 || height < 1 || width > 320 || height > 240) return false;
    const size_t row = size_t(width) * 3;
    if (stride < row || stride > (std::numeric_limits<size_t>::max() - row) / size_t(height)) return false;
    if (readableBytes < stride * size_t(height - 1) + row) return false;
    bgra.resize(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const uint8_t* source = rgb + size_t(y) * stride + size_t(x) * 3;
            uint8_t* target = bgra.data() + (size_t(y) * width + x) * 4;
            target[0] = source[2]; target[1] = source[1]; target[2] = source[0]; target[3] = 255;
        }
    return true;
}
