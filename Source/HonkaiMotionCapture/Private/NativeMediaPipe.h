// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace HonkaiCaptureNative
{
struct Landmark
{
    float x = 0, y = 0, z = 0, visibility = 0, presence = 0;
};
struct Hand
{
    std::string handedness;
    float score = 0;
    std::vector<Landmark> landmarks, worldLandmarks;
};
struct Frame
{
    bool faceTracked = false, poseTracked = false;
    std::array<float, 4> faceRotationRH{0, 0, 0, 1};
    std::map<std::string, float> blendShapes;
    std::vector<Landmark> poseLandmarks, poseWorldLandmarks;
    std::vector<Hand> hands;
};

// Thread-confined: initialize, detect and destroy on the owning inference worker.
class MediaPipe
{
public:
    MediaPipe();
    ~MediaPipe();
    MediaPipe(const MediaPipe&) = delete;
    MediaPipe& operator=(const MediaPipe&) = delete;

    bool Initialize(const std::wstring& dll, const std::string& faceModel,
        const std::string& handModel, const std::string& poseModel, float confidence, std::string& error);
    // rgb must cover stride * height readable bytes; pixel data is not retained.
    bool DetectRgb(const uint8_t* rgb, int width, int height, int stride,
        int64_t timestampMs, Frame& out, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
