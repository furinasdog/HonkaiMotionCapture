// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

enum class EHonkaiCaptureState : uint8 { Stopped, Starting, Running, Stopping, Error };

struct FHonkaiCaptureSettings
{
    int32 CameraIndex = 0;
    int32 Width = 640;
    int32 Height = 480;
    int32 FramesPerSecond = 30;
    float Confidence = .5f;
};

struct FHonkaiCaptureLandmark
{
    FVector3f Position = FVector3f::ZeroVector;
    float Visibility = 0;
    float Presence = 0;
};

struct FHonkaiCaptureHand
{
    FName Handedness;
    float Score = 0;
    TArray<FHonkaiCaptureLandmark> Landmarks;
    TArray<FHonkaiCaptureLandmark> WorldLandmarks;
};

struct FHonkaiCaptureFrame
{
    uint64 Sequence = 0;
    double TimestampSeconds = 0;
    EHonkaiCaptureState State = EHonkaiCaptureState::Stopped;
    bool bFaceTracked = false;
    bool bPoseTracked = false;
    // Raw MediaPipe right-handed camera quaternion. The consuming rig owns basis conversion and mirroring.
    FQuat4f FaceRotationRH = FQuat4f::Identity;
    TMap<FName, float> BlendShapes;
    TArray<FHonkaiCaptureLandmark> PoseLandmarks;
    TArray<FHonkaiCaptureLandmark> PoseWorldLandmarks;
    TArray<FHonkaiCaptureHand> Hands;
    float InferenceMilliseconds = 0;
    float ProcessingFPS = 0;
    int32 Width = 0;
    int32 Height = 0;
    FString Status;
    FString Error;
};

// Start/Stop/Poll belong to the caller's game thread. Stop requests cooperative shutdown and returns immediately.
class HONKAIMOTIONCAPTURE_API FHonkaiMotionCapture
{
public:
    FHonkaiMotionCapture();
    ~FHonkaiMotionCapture();
    FHonkaiMotionCapture(const FHonkaiMotionCapture&) = delete;
    FHonkaiMotionCapture& operator=(const FHonkaiMotionCapture&) = delete;

    // True queues startup; Poll reports Starting/Running/Error after worker initialization.
    bool Start(const FHonkaiCaptureSettings& Settings, FString& Error);
    void Stop();
    bool IsRunning() const;
    bool IsStopping() const;
    bool Poll(FHonkaiCaptureFrame& Frame);
    FString GetStatus() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
