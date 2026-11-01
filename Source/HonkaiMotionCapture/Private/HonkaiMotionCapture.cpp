// SPDX-License-Identifier: MIT
#include "HonkaiMotionCapture.h"
#include "NativeMediaPipe.h"
#include "OpenCvRuntimeCheck.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Windows/WindowsPlatformMisc.h"

#include "PreOpenCVHeaders.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include "PostOpenCVHeaders.h"
#include <stdexcept>

namespace
{
    struct FFrameState
    {
        mutable FCriticalSection Mutex;
        FHonkaiCaptureFrame Latest;
        uint64 Sequence = 0;

        void PublishLocked(FHonkaiCaptureFrame Frame)
        {
            Frame.Sequence = ++Sequence;
            Frame.TimestampSeconds = FMath::Max(Frame.TimestampSeconds > 0 ? Frame.TimestampSeconds : FPlatformTime::Seconds(),
                Latest.TimestampSeconds + 1.e-9);
            Latest = MoveTemp(Frame);
        }
        void Publish(FHonkaiCaptureFrame Frame)
        {
            FScopeLock Lock(&Mutex); PublishLocked(MoveTemp(Frame));
        }
        void Status(EHonkaiCaptureState State, const FString& Text, const FString& Error = FString())
        {
            FHonkaiCaptureFrame Frame; Frame.State = State; Frame.Status = Text; Frame.Error = Error;
            Publish(MoveTemp(Frame));
        }
        bool Poll(uint64& LastSequence, FHonkaiCaptureFrame& Out)
        {
            FScopeLock Lock(&Mutex);
            if (Latest.Sequence <= LastSequence) return false;
            Out = Latest; LastSequence = Latest.Sequence; return true;
        }
        FString GetStatus() const
        {
            FScopeLock Lock(&Mutex);
            return Latest.Status.IsEmpty() ? TEXT("动捕未启动。") : Latest.Status;
        }
    };

    void CopyLandmarks(const std::vector<HonkaiCaptureNative::Landmark>& Source, TArray<FHonkaiCaptureLandmark>& Out)
    {
        Out.Reserve(int32(Source.size()));
        for (const auto& P : Source)
        {
            auto& D = Out.AddDefaulted_GetRef();
            D.Position = FVector3f(P.x, P.y, P.z); D.Visibility = P.visibility; D.Presence = P.presence;
        }
    }

    class FCaptureWorker : public FRunnable
    {
    public:
        TSharedRef<FFrameState, ESPMode::ThreadSafe> State;
        FHonkaiCaptureSettings Settings;
        FString Root;
        FRunnableThread* Thread = nullptr;
        FEvent* Wake = FPlatformProcess::GetSynchEventFromPool(false);
        TAtomic<bool> bStop{false}, bFinished{false};

        FCaptureWorker(const TSharedRef<FFrameState, ESPMode::ThreadSafe>& InState,
            const FHonkaiCaptureSettings& InSettings, const FString& InRoot)
            : State(InState), Settings(InSettings), Root(InRoot) {}
        ~FCaptureWorker()
        {
            if (Thread) { Thread->WaitForCompletion(); delete Thread; }
            FPlatformProcess::ReturnSynchEventToPool(Wake);
        }
        virtual void Stop() override
        {
            {
                FScopeLock Lock(&State->Mutex);
                if (bFinished.Load()) return;
                bStop.Store(true);
                FHonkaiCaptureFrame Frame; Frame.State = EHonkaiCaptureState::Stopping;
                Frame.Status = TEXT("正在停止动捕并释放摄像头…"); State->PublishLocked(MoveTemp(Frame));
            }
            Wake->Trigger();
        }
        void PublishActive(FHonkaiCaptureFrame Frame)
        {
            FScopeLock Lock(&State->Mutex);
            if (!bStop.Load()) State->PublishLocked(MoveTemp(Frame));
        }
        virtual uint32 Run() override
        {
            FString Failure;
            const bool bCom = FWindowsPlatformMisc::CoInitialize(ECOMModel::Multithreaded);
            try
            {
                if (!bCom) throw std::runtime_error("Cannot initialize the capture thread COM apartment.");
                HonkaiCaptureNative::MediaPipe SDK;
                std::string Error;
                const FString Dll = FPaths::ConvertRelativePathToFull(Root / TEXT("Binaries/ThirdParty/Win64/libmediapipe.dll"));
                const FString Models = FPaths::ConvertRelativePathToFull(Root / TEXT("Resources/Models"));
                if (!SDK.Initialize(std::wstring(*Dll), TCHAR_TO_UTF8(*(Models / TEXT("face_landmarker.task"))),
                    TCHAR_TO_UTF8(*(Models / TEXT("hand_landmarker.task"))), TCHAR_TO_UTF8(*(Models / TEXT("pose_landmarker_lite.task"))),
                    Settings.Confidence, Error)) throw std::runtime_error(Error);
                if (!bStop.Load())
                {
                    cv::VideoCapture Camera;
                    if (!Camera.open(Settings.CameraIndex, cv::CAP_DSHOW)) throw std::runtime_error("Cannot open the selected camera.");
                    Camera.set(cv::CAP_PROP_FRAME_WIDTH, Settings.Width);
                    Camera.set(cv::CAP_PROP_FRAME_HEIGHT, Settings.Height);
                    Camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
                    Camera.set(cv::CAP_PROP_FPS, Settings.FramesPerSecond);
                    FHonkaiCaptureFrame Ready; Ready.State = EHonkaiCaptureState::Running;
                    Ready.Status = TEXT("正在等待摄像头画面…"); PublishActive(MoveTemp(Ready));
                    int32 EmptyFrames = 0;
                    double PreviousFrame = 0;
                    float MeasuredFPS = 0;
                    while (!bStop.Load())
                    {
                        const double Started = FPlatformTime::Seconds();
                        cv::Mat Bgr, Rgb;
                        if (!Camera.read(Bgr) || Bgr.empty())
                        {
                            if (++EmptyFrames > 30) throw std::runtime_error("The camera did not provide readable frames.");
                            Wake->Wait(10); continue;
                        }
                        EmptyFrames = 0;
                        if (Bgr.depth() != CV_8U || (Bgr.channels() != 1 && Bgr.channels() != 3 && Bgr.channels() != 4))
                            throw std::runtime_error("The camera returned an unsupported pixel format.");
                        const double Captured = FPlatformTime::Seconds();
                        if (Bgr.channels() == 4) cv::cvtColor(Bgr, Rgb, cv::COLOR_BGRA2RGB);
                        else if (Bgr.channels() == 1) cv::cvtColor(Bgr, Rgb, cv::COLOR_GRAY2RGB);
                        else cv::cvtColor(Bgr, Rgb, cv::COLOR_BGR2RGB);
                        if (bStop.Load()) break;
                        HonkaiCaptureNative::Frame Native;
                        const double InferenceStarted = FPlatformTime::Seconds();
                        if (!SDK.DetectRgb(Rgb.data, Rgb.cols, Rgb.rows, int(Rgb.step), int64(Captured * 1000.), Native, Error))
                            throw std::runtime_error(Error);
                        const double Completed = FPlatformTime::Seconds();
                        if (bStop.Load()) break;
                        if (PreviousFrame > 0)
                        {
                            const float FPS = float(1. / FMath::Max(Completed - PreviousFrame, 1.e-6));
                            MeasuredFPS = MeasuredFPS > 0 ? FMath::Lerp(MeasuredFPS, FPS, .1f) : FPS;
                        }
                        PreviousFrame = Completed;
                        FHonkaiCaptureFrame Frame;
                        Frame.State = EHonkaiCaptureState::Running; Frame.TimestampSeconds = Captured;
                        Frame.bFaceTracked = Native.faceTracked; Frame.bPoseTracked = Native.poseTracked;
                        Frame.FaceRotationRH = FQuat4f(Native.faceRotationRH[0], Native.faceRotationRH[1], Native.faceRotationRH[2], Native.faceRotationRH[3]);
                        for (const auto& B : Native.blendShapes) Frame.BlendShapes.Add(FName(UTF8_TO_TCHAR(B.first.c_str())), B.second);
                        CopyLandmarks(Native.poseLandmarks, Frame.PoseLandmarks); CopyLandmarks(Native.poseWorldLandmarks, Frame.PoseWorldLandmarks);
                        for (const auto& H : Native.hands)
                        {
                            auto& Hand = Frame.Hands.AddDefaulted_GetRef(); Hand.Handedness = FName(UTF8_TO_TCHAR(H.handedness.c_str())); Hand.Score = H.score;
                            CopyLandmarks(H.landmarks, Hand.Landmarks); CopyLandmarks(H.worldLandmarks, Hand.WorldLandmarks);
                        }
                        Frame.InferenceMilliseconds = float((Completed - InferenceStarted) * 1000.);
                        Frame.ProcessingFPS = MeasuredFPS; Frame.Width = Rgb.cols; Frame.Height = Rgb.rows;
                        Frame.Status = Frame.bFaceTracked || Frame.bPoseTracked || !Frame.Hands.IsEmpty() ? TEXT("动捕追踪中") : TEXT("未检测到有效人体或面部");
                        PublishActive(MoveTemp(Frame));
                        const double Delay = 1. / Settings.FramesPerSecond - (FPlatformTime::Seconds() - Started);
                        if (Delay > 0) Wake->Wait(uint32(FMath::CeilToDouble(Delay * 1000.)));
                    }
                    // Camera and SDK are released by this same worker, never by the game thread.
                }
            }
            catch (const std::exception& E) { Failure = UTF8_TO_TCHAR(E.what()); }
            catch (...) { Failure = TEXT("未知的原生动捕错误。"); }
            if (bCom) FWindowsPlatformMisc::CoUninitialize();
            {
                FScopeLock Lock(&State->Mutex);
                FHonkaiCaptureFrame Final;
                Final.State = !Failure.IsEmpty() && !bStop.Load() ? EHonkaiCaptureState::Error : EHonkaiCaptureState::Stopped;
                Final.Error = Final.State == EHonkaiCaptureState::Error ? Failure : FString();
                Final.Status = Final.State == EHonkaiCaptureState::Error ? TEXT("动捕失败：") + Failure : TEXT("动捕已停止。");
                State->PublishLocked(MoveTemp(Final)); bFinished.Store(true);
            }
            return Failure.IsEmpty() ? 0 : 1;
        }
    };

    FCriticalSection WorkersMutex;
    TArray<TSharedPtr<FCaptureWorker, ESPMode::ThreadSafe>> Workers;

    void ReapWorkers()
    {
        FScopeLock Lock(&WorkersMutex);
        Workers.RemoveAll([](const auto& W) { return W->bFinished.Load(); });
    }
}

struct FHonkaiMotionCapture::FImpl
{
    TSharedRef<FFrameState, ESPMode::ThreadSafe> State = MakeShared<FFrameState, ESPMode::ThreadSafe>();
    TSharedPtr<FCaptureWorker, ESPMode::ThreadSafe> Worker;
    uint64 LastPolledSequence = 0;
};

FHonkaiMotionCapture::FHonkaiMotionCapture() : Impl(MakeUnique<FImpl>()) {}
FHonkaiMotionCapture::~FHonkaiMotionCapture() { Stop(); }

bool FHonkaiMotionCapture::Start(const FHonkaiCaptureSettings& Settings, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread()) { Error = TEXT("请在游戏线程启动动捕。"); return false; }
    if (IsRunning()) { Error = IsStopping() ? TEXT("上一次动捕仍在释放资源，请等待停止完成。") : TEXT("动捕已在运行。"); return false; }
    if (Settings.CameraIndex < 0 || Settings.CameraIndex > 32 || Settings.Width < 160 || Settings.Width > 3840
        || Settings.Height < 120 || Settings.Height > 2160 || Settings.FramesPerSecond < 1 || Settings.FramesPerSecond > 60
        || !FMath::IsFinite(Settings.Confidence) || Settings.Confidence <= 0 || Settings.Confidence > 1)
    { Error = TEXT("摄像头参数或置信度超出支持范围。"); return false; }
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("HonkaiMotionCapture"));
    if (!Plugin.IsValid()) { Error = TEXT("无法定位 HonkaiMotionCapture 插件。"); return false; }
    const FString Root = Plugin->GetBaseDir();
    const TCHAR* Required[] = {TEXT("Binaries/ThirdParty/Win64/libmediapipe.dll"), TEXT("Resources/Models/face_landmarker.task"),
        TEXT("Resources/Models/hand_landmarker.task"), TEXT("Resources/Models/pose_landmarker_lite.task")};
    for (const TCHAR* File : Required)
        if (!FPaths::FileExists(Root / File)) { Error = TEXT("动捕运行库或模型尚未准备，请运行插件 Scripts/Stage-LocalRuntime.ps1。"); return false; }
    if (!FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("OpenCVHelper")))
    { Error = TEXT("UE OpenCV 模块不可用。"); return false; }
    const auto OpenCVPlugin = IPluginManager::Get().FindPlugin(TEXT("OpenCV"));
    const FString OpenCVPath = OpenCVPlugin.IsValid() ? OpenCVPlugin->GetBaseDir() / TEXT(UE_STRINGIZE(OPENCV_PLATFORM_PATH)) / TEXT(UE_STRINGIZE(OPENCV_DLL_NAME)) : FString();
    if (!HonkaiCaptureNative::IsOpenCvRuntimeLoaded(*OpenCVPath))
    { Error = TEXT("UE OpenCV 原生运行库未成功加载。"); return false; }
    ReapWorkers(); Impl->Worker.Reset();
    Impl->State->Status(EHonkaiCaptureState::Starting, TEXT("正在初始化动捕…"));
    Impl->Worker = MakeShared<FCaptureWorker, ESPMode::ThreadSafe>(Impl->State, Settings, Root);
    Impl->Worker->Thread = FRunnableThread::Create(Impl->Worker.Get(), TEXT("HonkaiMotionCapture"), 0, TPri_BelowNormal);
    if (!Impl->Worker->Thread)
    {
        Error = TEXT("无法创建动捕工作线程。"); Impl->Worker.Reset();
        Impl->State->Status(EHonkaiCaptureState::Error, Error, Error); return false;
    }
    { FScopeLock Lock(&WorkersMutex); Workers.Add(Impl->Worker); }
    return true;
}

void FHonkaiMotionCapture::Stop()
{
    if (!IsRunning()) return;
    Impl->Worker->Stop();
}
bool FHonkaiMotionCapture::IsRunning() const { return Impl->Worker.IsValid() && !Impl->Worker->bFinished.Load(); }
bool FHonkaiMotionCapture::IsStopping() const { return IsRunning() && Impl->Worker->bStop.Load(); }
bool FHonkaiMotionCapture::Poll(FHonkaiCaptureFrame& Frame) { return Impl->State->Poll(Impl->LastPolledSequence, Frame); }
FString FHonkaiMotionCapture::GetStatus() const { return Impl->State->GetStatus(); }

void ShutdownHonkaiCaptureWorkers()
{
    TArray<TSharedPtr<FCaptureWorker, ESPMode::ThreadSafe>> Active;
    { FScopeLock Lock(&WorkersMutex); Active = MoveTemp(Workers); }
    for (const auto& Worker : Active) Worker->Stop();
    for (const auto& Worker : Active) if (Worker->Thread) Worker->Thread->WaitForCompletion();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiCaptureFrameTest, "HonkaiMotionCapture.InMemorySnapshots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FHonkaiCaptureFrameTest::RunTest(const FString& Parameters)
{
    FFrameState State;
    uint64 Last = 0;
    FHonkaiCaptureFrame Out;
    TestFalse(TEXT("No frame is distinct from an empty tracked result"), State.Poll(Last, Out));
    FHonkaiCaptureFrame A; A.bFaceTracked = true; A.Width = 640; A.PoseLandmarks.SetNum(33);
    A.PoseLandmarks[11].Position.X = .7f; State.Publish(A);
    TestTrue(TEXT("Complete in-memory snapshot is available"), State.Poll(Last, Out));
    const double FirstTime = Out.TimestampSeconds;
    TestTrue(TEXT("Face and body share one complete frame"), Out.bFaceTracked && Out.Width == 640 && Out.PoseLandmarks.Num() == 33 && Out.PoseLandmarks[11].Position.X == .7f);
    TestFalse(TEXT("Polling without a new result returns no frame"), State.Poll(Last, Out));
    State.Status(EHonkaiCaptureState::Stopping, TEXT("stopping"));
    TestTrue(TEXT("Stopping produces an explicit state snapshot"), State.Poll(Last, Out) && Out.State == EHonkaiCaptureState::Stopping);
    TestTrue(TEXT("Stopping does not replay old tracking"), !Out.bFaceTracked && Out.PoseLandmarks.IsEmpty());
    TestTrue(TEXT("Snapshot timestamps are monotonic"), Out.TimestampSeconds > FirstTime);
    State.Publish(A); A.Width = 1280; State.Publish(A);
    TestTrue(TEXT("Polling keeps the newest complete frame"), State.Poll(Last, Out) && Out.Width == 1280 && Out.Sequence == 4);
    {
        const auto Shared = MakeShared<FFrameState, ESPMode::ThreadSafe>();
        FCaptureWorker Worker(Shared, FHonkaiCaptureSettings{}, FString());
        Worker.Stop(); Worker.PublishActive(A);
        uint64 Sequence = 0;
        TestTrue(TEXT("A late inference result cannot overwrite a stop request"), Shared->Poll(Sequence, Out)
            && Out.State == EHonkaiCaptureState::Stopping && !Out.bFaceTracked);
        Worker.bFinished.Store(true);
        Shared->Status(EHonkaiCaptureState::Stopped, TEXT("stopped"));
        Worker.Stop();
        TestTrue(TEXT("Stopping a finished worker cannot regress its final state"), Shared->Poll(Sequence, Out) && Out.State == EHonkaiCaptureState::Stopped);
    }
    FHonkaiMotionCapture Capture; FHonkaiCaptureSettings Invalid; Invalid.CameraIndex = -1; FString Error;
    TestFalse(TEXT("Invalid settings fail before any camera or SDK access"), Capture.Start(Invalid, Error));
    TestFalse(TEXT("Failed startup is not reported as running"), Capture.IsRunning());
    Capture.Stop(); Capture.Stop();
    return true;
}
#endif
