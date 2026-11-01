// SPDX-License-Identifier: MIT
#include "NativeMediaPipe.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define MP_EXPORT
#include "mediapipe/tasks/c/core/common.h"
#include "mediapipe/tasks/c/vision/core/image.h"
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>

struct FixtureImage
{
    HMODULE library = nullptr;
    MpImagePtr image = nullptr;
    decltype(&MpImageFree) imageFree = nullptr;
    const uint8_t* rgb = nullptr;
    int width = 0, height = 0, stride = 0;
    ~FixtureImage() { Close(); }
    void Close()
    {
        if (image) { imageFree(image); image = nullptr; }
        if (library) { FreeLibrary(library); library = nullptr; }
    }
    bool Read(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        library = LoadLibraryExW((root / L"libmediapipe.dll").c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) return false;
        const auto create = reinterpret_cast<decltype(&MpImageCreateFromFile)>(GetProcAddress(library, "MpImageCreateFromFile"));
        const auto data = reinterpret_cast<decltype(&MpImageDataUint8)>(GetProcAddress(library, "MpImageDataUint8"));
        const auto errorFree = reinterpret_cast<decltype(&MpErrorFree)>(GetProcAddress(library, "MpErrorFree"));
        const auto getWidth = reinterpret_cast<decltype(&MpImageGetWidth)>(GetProcAddress(library, "MpImageGetWidth"));
        const auto getHeight = reinterpret_cast<decltype(&MpImageGetHeight)>(GetProcAddress(library, "MpImageGetHeight"));
        const auto getStride = reinterpret_cast<decltype(&MpImageGetWidthStep)>(GetProcAddress(library, "MpImageGetWidthStep"));
        const auto format = reinterpret_cast<decltype(&MpImageGetFormat)>(GetProcAddress(library, "MpImageGetFormat"));
        imageFree = reinterpret_cast<decltype(&MpImageFree)>(GetProcAddress(library, "MpImageFree"));
        if (!create || !data || !errorFree || !getWidth || !getHeight || !getStride || !format || !imageFree) return false;
        char* error = nullptr;
        const auto filename = path.u8string();
        auto status = create(filename.c_str(), &image, &error);
        if (error) { std::cerr << error << '\n'; errorFree(error); error = nullptr; }
        if (status != kMpOk || !image || format(image) != kMpImageFormatSrgb) return false;
        status = data(image, &rgb, &error);
        if (error) { std::cerr << error << '\n'; errorFree(error); }
        width = getWidth(image); height = getHeight(image); stride = getStride(image);
        return status == kMpOk && rgb && width > 0 && height > 0 && stride >= width * 3;
    }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) { std::cerr << "Usage: NativeSdkSmoke <prepared native bin directory> <fixture image>\n"; return 2; }
    const std::filesystem::path root = std::filesystem::absolute(argv[1]);
    const auto file = [](const std::filesystem::path& path) { return path.u8string(); };
    FixtureImage image;
    if (!image.Read(root, std::filesystem::absolute(argv[2]))) { std::cerr << "Cannot read RGB fixture image\n"; return 2; }
    const int width = image.width, height = image.height;
    std::vector<uint8_t> rgb(size_t(width) * height * 3);
    for (int row = 0; row < height; ++row) std::memcpy(rgb.data() + size_t(row) * width * 3, image.rgb + size_t(row) * image.stride, size_t(width) * 3);
    image.Close();
    for (int round = 0; round < 2; ++round)
    {
        auto sdk = std::make_unique<HonkaiCaptureNative::MediaPipe>();
        HonkaiCaptureNative::Frame frame;
        std::string error;
        if (sdk->DetectRgb(rgb.data(), width, height, width * 3, 0, frame, error)) return 3;
        if (sdk->Initialize((root / L"missing.dll").wstring(), "bad", "bad", "bad", .5f, error)) return 4;
        const auto initStart = std::chrono::steady_clock::now();
        if (!sdk->Initialize((root / L"libmediapipe.dll").wstring(), file(root / L"Models/face_landmarker.task"),
            file(root / L"Models/hand_landmarker.task"), file(root / L"Models/pose_landmarker_lite.task"), .5f, error))
        { std::cerr << "Initialize: " << error << '\n'; return 5; }
        std::cout << "round=" << round << " initialize_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initStart).count() << '\n';
        int poseFrames = 0;
        for (int i = 0; i < 3; ++i)
        {
            const int stride = width * 3 + 7;
            std::vector<uint8_t> padded(size_t(stride) * height, 0);
            for (int row = 0; row < height; ++row) std::memcpy(padded.data() + size_t(row) * stride, rgb.data() + size_t(row) * width * 3, size_t(width) * 3);
            const auto inferStart = std::chrono::steady_clock::now();
            if (!sdk->DetectRgb(padded.data(), width, height, stride, i * 33, frame, error, i != 2))
            { std::cerr << "Detect: " << error << '\n'; return 6; }
            double norm = 0; for (float v : frame.faceRotationRH) { if (!std::isfinite(v)) return 7; norm += v * v; }
            std::cout << "round=" << round << " frame=" << i << " face=" << frame.faceTracked << " blendshapes=" << frame.blendShapes.size()
                << " pose=" << frame.poseLandmarks.size() << " world_pose=" << frame.poseWorldLandmarks.size()
                << " hands=" << frame.hands.size() << " quaternion_norm=" << std::sqrt(norm)
                << " infer_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inferStart).count()
                << " face_quaternion_rh=" << frame.faceRotationRH[0] << ',' << frame.faceRotationRH[1] << ',' << frame.faceRotationRH[2] << ',' << frame.faceRotationRH[3] << '\n';
            if (!frame.faceTracked || frame.blendShapes.size() != 52 || frame.hands.size() != 2 || std::abs(norm - 1) > .0001) return 8;
            if (i == 2 ? !frame.faceLandmarks.empty() : frame.faceLandmarks.size() != 478) return 11;
            if (frame.poseTracked) { if (frame.poseLandmarks.size() != 33 || frame.poseWorldLandmarks.size() != 33) return 8; ++poseFrames; }
            else if (!frame.poseLandmarks.empty() || !frame.poseWorldLandmarks.empty()) return 8;
            for (const auto& hand : frame.hands)
            {
                std::cout << "hand=" << hand.handedness << " score=" << hand.score << " points=" << hand.landmarks.size() << " world_points=" << hand.worldLandmarks.size() << '\n';
                if (hand.landmarks.size() != 21 || hand.worldLandmarks.size() != 21 || !std::isfinite(hand.score) || hand.score < .5f) return 9;
            }
        }
        if (!poseFrames) return 8;
        if (sdk->DetectRgb(rgb.data(), width, height, width * 3, 66, frame, error) || frame.faceTracked || !frame.hands.empty()) return 10;
        const auto closeStart = std::chrono::steady_clock::now();
        sdk.reset();
        std::cout << "round=" << round << " pose_detected_frames=" << poseFrames << "/3 close_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - closeStart).count() << '\n';
    }
    std::cout << "PASS real static-image VIDEO inference, RGB stride, SDK failures, timestamp guard; no frame IPC\n";
    return 0;
}
