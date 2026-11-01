// SPDX-License-Identifier: MIT
#include "NativeMediaPipe.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(HONKAI_CAPTURE_UNREAL) && HONKAI_CAPTURE_UNREAL
#include "Windows/WindowsHWrapper.h"
#else
#include <windows.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#pragma push_macro("MP_EXPORT")
#undef MP_EXPORT
#define MP_EXPORT
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/c/vision/hand_landmarker/hand_landmarker.h"
#include "mediapipe/tasks/c/vision/pose_landmarker/pose_landmarker.h"
#pragma pop_macro("MP_EXPORT")

namespace HonkaiCaptureNative
{
namespace
{
void SetError(std::string& error, const char* message) noexcept
{
    try { error = message; }
    catch (...) { error.clear(); }
}
template<class Result, class CloseFunction> struct ResultScope
{
    Result value{};
    CloseFunction close;
    explicit ResultScope(CloseFunction function) : close(function) {}
    ~ResultScope() { close(&value); }
};

template<class List> std::vector<Landmark> ReadPoints(const List& list, uint32_t count)
{
    if (list.landmarks_count != count || !list.landmarks) throw std::runtime_error("Unexpected MediaPipe landmark array");
    std::vector<Landmark> points;
    points.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& p = list.landmarks[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
            || std::abs(p.x) > 1.e6f || std::abs(p.y) > 1.e6f || std::abs(p.z) > 1.e6f
            || (p.has_visibility && !std::isfinite(p.visibility)) || (p.has_presence && !std::isfinite(p.presence)))
            throw std::runtime_error("Non-finite MediaPipe landmark");
        points.push_back({p.x, p.y, p.z,
            p.has_visibility ? std::clamp(p.visibility, 0.f, 1.f) : 0.f,
            p.has_presence ? std::clamp(p.presence, 0.f, 1.f) : 0.f});
    }
    return points;
}

std::array<float, 4> RotationQuaternion(const Matrix& matrix)
{
    if (matrix.rows != 4 || matrix.cols != 4 || !matrix.data) throw std::runtime_error("Unexpected MediaPipe face matrix");
    double r[3][3];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix.data[col * 4 + row])) throw std::runtime_error("Non-finite MediaPipe face matrix");
    for (int col = 0; col < 3; ++col)
    {
        double length = 0;
        for (int row = 0; row < 3; ++row) length += double(matrix.data[col * 4 + row]) * matrix.data[col * 4 + row];
        length = std::sqrt(length);
        if (length < 1.e-6) throw std::runtime_error("Degenerate MediaPipe face rotation");
        for (int row = 0; row < 3; ++row) r[row][col] = matrix.data[col * 4 + row] / length;
    }
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b)
            if (std::abs(r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b]) > .01)
                throw std::runtime_error("Non-orthogonal MediaPipe face rotation");
    const double determinant = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1])
        - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (determinant < .99) throw std::runtime_error("Reflected MediaPipe face rotation");
    double x, y, z, w;
    const double trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0)
    {
        const double s = std::sqrt(trace + 1) * 2;
        w = .25 * s; x = (r[2][1] - r[1][2]) / s; y = (r[0][2] - r[2][0]) / s; z = (r[1][0] - r[0][1]) / s;
    }
    else if (r[0][0] > r[1][1] && r[0][0] > r[2][2])
    {
        const double s = std::sqrt(1 + r[0][0] - r[1][1] - r[2][2]) * 2;
        w = (r[2][1] - r[1][2]) / s; x = .25 * s; y = (r[0][1] + r[1][0]) / s; z = (r[0][2] + r[2][0]) / s;
    }
    else if (r[1][1] > r[2][2])
    {
        const double s = std::sqrt(1 + r[1][1] - r[0][0] - r[2][2]) * 2;
        w = (r[0][2] - r[2][0]) / s; x = (r[0][1] + r[1][0]) / s; y = .25 * s; z = (r[1][2] + r[2][1]) / s;
    }
    else
    {
        const double s = std::sqrt(1 + r[2][2] - r[0][0] - r[1][1]) * 2;
        w = (r[1][0] - r[0][1]) / s; x = (r[0][2] + r[2][0]) / s; y = (r[1][2] + r[2][1]) / s; z = .25 * s;
    }
    double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (!std::isfinite(length) || length < 1.e-6) throw std::runtime_error("Invalid MediaPipe face quaternion");
    if (w < 0) length = -length;
    return {float(x / length), float(y / length), float(z / length), float(w / length)};
}
}

struct MediaPipe::Impl
{
    HMODULE library = nullptr;
    MpFaceLandmarkerPtr face = nullptr;
    MpHandLandmarkerPtr hand = nullptr;
    MpPoseLandmarkerPtr pose = nullptr;
    std::string facePath, handPath, posePath;
    std::vector<uint8_t> packedRgb;
    int64_t lastTimestamp = -1;

    decltype(&MpErrorFree) errorFree = nullptr;
    decltype(&MpImageCreateFromUint8Data) createImage = nullptr;
    decltype(&MpImageFree) imageFree = nullptr;
    decltype(&MpFaceLandmarkerCreate) faceCreate = nullptr;
    decltype(&MpFaceLandmarkerClose) faceClose = nullptr;
    decltype(&MpFaceLandmarkerDetectForVideo) faceDetect = nullptr;
    decltype(&MpFaceLandmarkerCloseResult) faceCloseResult = nullptr;
    decltype(&MpHandLandmarkerCreate) handCreate = nullptr;
    decltype(&MpHandLandmarkerClose) handClose = nullptr;
    decltype(&MpHandLandmarkerDetectForVideo) handDetect = nullptr;
    decltype(&MpHandLandmarkerCloseResult) handCloseResult = nullptr;
    decltype(&MpPoseLandmarkerCreate) poseCreate = nullptr;
    decltype(&MpPoseLandmarkerClose) poseClose = nullptr;
    decltype(&MpPoseLandmarkerDetectForVideo) poseDetect = nullptr;
    decltype(&MpPoseLandmarkerCloseResult) poseCloseResult = nullptr;

    ~Impl()
    {
        char* message = nullptr;
        if (pose) { poseClose(pose, &message); if (message) errorFree(message); message = nullptr; }
        if (hand) { handClose(hand, &message); if (message) errorFree(message); message = nullptr; }
        if (face) { faceClose(face, &message); if (message) errorFree(message); }
        if (library) FreeLibrary(library);
    }
    template<class Function> Function Load(const char* name)
    {
        const auto function = GetProcAddress(library, name);
        if (!function) throw std::runtime_error(std::string("Missing MediaPipe 0.10.35 export: ") + name);
        return reinterpret_cast<Function>(function);
    }
    void Check(MpStatus status, char* message)
    {
        const std::unique_ptr<char, decltype(errorFree)> owned(message, errorFree);
        const std::string detail = message ? message : "MediaPipe call failed without details";
        if (status != kMpOk) throw std::runtime_error(detail);
    }
};

MediaPipe::MediaPipe() = default;
MediaPipe::~MediaPipe() = default;

bool MediaPipe::Initialize(const std::wstring& dll, const std::string& faceModel,
    const std::string& handModel, const std::string& poseModel, float confidence, std::string& error)
{
    impl.reset(); error.clear();
    try
    {
        if (dll.empty() || dll.find(L'\0') != std::wstring::npos || faceModel.empty() || handModel.empty() || poseModel.empty()
            || faceModel.find('\0') != std::string::npos || handModel.find('\0') != std::string::npos || poseModel.find('\0') != std::string::npos
            || !std::isfinite(confidence) || confidence < 0 || confidence > 1)
            throw std::runtime_error("Invalid MediaPipe initialization settings");
        auto state = std::make_unique<Impl>();
        state->library = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!state->library) throw std::runtime_error("Cannot load the prepared libmediapipe.dll");
#define BIND(member, function) state->member = state->Load<decltype(state->member)>(#function)
        BIND(errorFree, MpErrorFree); BIND(createImage, MpImageCreateFromUint8Data); BIND(imageFree, MpImageFree);
        BIND(faceCreate, MpFaceLandmarkerCreate); BIND(faceClose, MpFaceLandmarkerClose);
        BIND(faceDetect, MpFaceLandmarkerDetectForVideo); BIND(faceCloseResult, MpFaceLandmarkerCloseResult);
        BIND(handCreate, MpHandLandmarkerCreate); BIND(handClose, MpHandLandmarkerClose);
        BIND(handDetect, MpHandLandmarkerDetectForVideo); BIND(handCloseResult, MpHandLandmarkerCloseResult);
        BIND(poseCreate, MpPoseLandmarkerCreate); BIND(poseClose, MpPoseLandmarkerClose);
        BIND(poseDetect, MpPoseLandmarkerDetectForVideo); BIND(poseCloseResult, MpPoseLandmarkerCloseResult);
#undef BIND
        state->facePath = faceModel; state->handPath = handModel; state->posePath = poseModel;
        FaceLandmarkerOptions f{};
        f.base_options.model_asset_path = state->facePath.c_str(); f.base_options.delegate = CPU;
        f.base_options.host_environment = HOST_ENVIRONMENT_UNKNOWN; f.base_options.host_system = HOST_SYSTEM_WINDOWS;
        f.running_mode = VIDEO; f.num_faces = 1;
        f.min_face_detection_confidence = f.min_face_presence_confidence = f.min_tracking_confidence = confidence;
        f.output_face_blendshapes = true; f.output_facial_transformation_matrixes = true;
        char* message = nullptr;
        MpStatus status = state->faceCreate(&f, &state->face, &message); state->Check(status, message);
        HandLandmarkerOptions h{};
        h.base_options.model_asset_path = state->handPath.c_str(); h.base_options.delegate = CPU;
        h.base_options.host_environment = HOST_ENVIRONMENT_UNKNOWN; h.base_options.host_system = HOST_SYSTEM_WINDOWS;
        h.running_mode = VIDEO; h.num_hands = 2;
        h.min_hand_detection_confidence = h.min_hand_presence_confidence = h.min_tracking_confidence = confidence;
        message = nullptr; status = state->handCreate(&h, &state->hand, &message); state->Check(status, message);
        PoseLandmarkerOptions p{};
        p.base_options.model_asset_path = state->posePath.c_str(); p.base_options.delegate = CPU;
        p.base_options.host_environment = HOST_ENVIRONMENT_UNKNOWN; p.base_options.host_system = HOST_SYSTEM_WINDOWS;
        p.running_mode = VIDEO; p.num_poses = 1;
        p.min_pose_detection_confidence = p.min_pose_presence_confidence = p.min_tracking_confidence = confidence;
        message = nullptr; status = state->poseCreate(&p, &state->pose, &message); state->Check(status, message);
        if (!state->face || !state->hand || !state->pose) throw std::runtime_error("MediaPipe returned an empty task handle");
        impl = std::move(state);
        return true;
    }
    catch (const std::bad_alloc&) { error.clear(); return false; }
    catch (const std::exception& failure) { SetError(error, failure.what()); return false; }
    catch (...) { SetError(error, "Unknown MediaPipe initialization failure"); return false; }
}

bool MediaPipe::DetectRgb(const uint8_t* rgb, int width, int height, int stride,
    int64_t timestampMs, Frame& out, std::string& error)
{
    out = Frame{}; error.clear();
    try
    {
        if (!impl) throw std::runtime_error("MediaPipe is not initialized");
        if (!rgb || width < 1 || width > 8192 || height < 1 || height > 8192 || stride < width * 3
            || stride > std::numeric_limits<int>::max() / height)
            throw std::runtime_error("Invalid RGB image buffer dimensions");
        if (timestampMs < 0 || timestampMs <= impl->lastTimestamp) throw std::runtime_error("MediaPipe video timestamps must increase strictly");
        const int bytes = width * height * 3;
        if (stride != width * 3)
        {
            impl->packedRgb.resize(bytes);
            for (int row = 0; row < height; ++row)
                std::memcpy(impl->packedRgb.data() + row * width * 3, rgb + size_t(row) * stride, size_t(width) * 3);
            rgb = impl->packedRgb.data();
        }
        struct ImageScope
        {
            MpImagePtr value = nullptr;
            decltype(&MpImageFree) close;
            ~ImageScope() { if (value) close(value); }
        } image{nullptr, impl->imageFree};
        char* message = nullptr;
        MpStatus status = impl->createImage(kMpImageFormatSrgb, width, height, rgb, bytes, &image.value, &message);
        impl->Check(status, message);
        if (!image.value) throw std::runtime_error("MediaPipe returned an empty image handle");
        impl->lastTimestamp = timestampMs;
        Frame next;
        {
            ResultScope<FaceLandmarkerResult, decltype(impl->faceCloseResult)> result(impl->faceCloseResult);
            message = nullptr; status = impl->faceDetect(impl->face, image.value, nullptr, timestampMs, &result.value, &message); impl->Check(status, message);
            const auto& f = result.value;
            if (f.face_landmarks_count > 1 || f.facial_transformation_matrixes_count > 1 || f.face_blendshapes_count > 1)
                throw std::runtime_error("Unexpected number of MediaPipe faces");
            if (f.face_landmarks_count != f.facial_transformation_matrixes_count || f.face_landmarks_count != f.face_blendshapes_count)
                throw std::runtime_error("Incomplete MediaPipe face arrays");
            if (f.face_landmarks_count)
            {
                if (!f.face_landmarks || f.facial_transformation_matrixes_count != 1 || !f.facial_transformation_matrixes
                    || f.face_blendshapes_count != 1 || !f.face_blendshapes)
                    throw std::runtime_error("Incomplete MediaPipe face result");
                next.faceRotationRH = RotationQuaternion(f.facial_transformation_matrixes[0]);
                const auto& categories = f.face_blendshapes[0];
                if (!categories.categories || categories.categories_count != 52) throw std::runtime_error("Unexpected MediaPipe face blendshape count");
                for (uint32_t i = 0; i < categories.categories_count; ++i)
                {
                    const auto& c = categories.categories[i];
                    if (!c.category_name || !std::isfinite(c.score)) throw std::runtime_error("Invalid MediaPipe face category");
                    next.blendShapes[c.category_name] = std::clamp(c.score, 0.f, 1.f);
                }
                next.faceTracked = true;
            }
        }
        {
            ResultScope<PoseLandmarkerResult, decltype(impl->poseCloseResult)> result(impl->poseCloseResult);
            message = nullptr; status = impl->poseDetect(impl->pose, image.value, nullptr, timestampMs, &result.value, &message); impl->Check(status, message);
            const auto& p = result.value;
            if (p.pose_landmarks_count > 1 || p.pose_world_landmarks_count > 1) throw std::runtime_error("Unexpected number of MediaPipe poses");
            if (p.pose_landmarks_count || p.pose_world_landmarks_count)
            {
                if (p.pose_landmarks_count != 1 || p.pose_world_landmarks_count != 1 || !p.pose_landmarks || !p.pose_world_landmarks)
                    throw std::runtime_error("Incomplete MediaPipe pose result");
                next.poseLandmarks = ReadPoints(p.pose_landmarks[0], 33);
                next.poseWorldLandmarks = ReadPoints(p.pose_world_landmarks[0], 33);
                next.poseTracked = true;
            }
        }
        {
            ResultScope<HandLandmarkerResult, decltype(impl->handCloseResult)> result(impl->handCloseResult);
            message = nullptr; status = impl->handDetect(impl->hand, image.value, nullptr, timestampMs, &result.value, &message); impl->Check(status, message);
            const auto& h = result.value;
            if (h.hand_landmarks_count > 2 || h.hand_landmarks_count != h.hand_world_landmarks_count || h.hand_landmarks_count != h.handedness_count)
                throw std::runtime_error("Incomplete MediaPipe hand result");
            if (h.hand_landmarks_count && (!h.hand_landmarks || !h.hand_world_landmarks || !h.handedness))
                throw std::runtime_error("Invalid MediaPipe hand arrays");
            for (uint32_t i = 0; i < h.hand_landmarks_count; ++i)
            {
                const auto& c = h.handedness[i];
                if (!c.categories || c.categories_count != 1 || !c.categories[0].category_name || !std::isfinite(c.categories[0].score))
                    throw std::runtime_error("Invalid MediaPipe handedness");
                Hand hand;
                hand.handedness = c.categories[0].category_name;
                if (hand.handedness != "Left" && hand.handedness != "Right") throw std::runtime_error("Unknown MediaPipe handedness");
                hand.score = std::clamp(c.categories[0].score, 0.f, 1.f);
                hand.landmarks = ReadPoints(h.hand_landmarks[i], 21);
                hand.worldLandmarks = ReadPoints(h.hand_world_landmarks[i], 21);
                next.hands.push_back(std::move(hand));
            }
        }
        out = std::move(next);
        return true;
    }
    catch (const std::bad_alloc&) { error.clear(); return false; }
    catch (const std::exception& failure) { SetError(error, failure.what()); return false; }
    catch (...) { SetError(error, "Unknown MediaPipe inference failure"); return false; }
}
}
