# MotionCapture

用于 Unreal Engine 的原生摄像头动捕插件，通过 MediaPipe C API 在 UE 进程内计算面部、身体姿态和双手关键点，向宿主提供完整的内存快照。

插件适合由 C++ 宿主实现 VTuber、姿态预览或交互应用。它提供识别结果；角色骨骼绑定、表情映射、校准、平滑和 UI 由宿主实现。仓库不附带角色模型。

## 功能与兼容性

- 单人面部姿态四元数、52 个命名表情通道。
- 单人身体姿态：33 个图像关键点及对应世界关键点。
- 最多两只手：每只 21 个图像关键点、世界关键点及左右手分类。
- 按需输出不超过 320 × 240 的 BGRA 摄像头预览和面部关键点。
- 工作线程采集、推理；游戏线程通过 `Poll` 读取最新完整结果，无外部 EXE 或文件 IPC。

| 项目 | 当前支持范围 |
| --- | --- |
| 平台 | Windows x64；不支持 Windows ARM64 |
| UE | 当前适配 UE 5.8 的 OpenCV 4.5.5 模块；其他版本需要验证 ABI 和构建配置 |
| 推理 | MediaPipe 0.10.35 C API，CPU，VIDEO 模式 |
| 摄像头 | OpenCV DirectShow；实际分辨率、帧率由设备和驱动决定 |
| 接口 | C++；当前没有蓝图封装或自带角色绑定器 |

仓库名为 **MotionCapture**。为保留 UE 项目兼容性，插件目录、`.uplugin`、模块名和 C++ 接口仍使用 `HonkaiMotionCapture`，安装时请显式指定目录。

## 快速开始

### 1. 安装插件

在 UE C++ 项目根目录运行：

```powershell
git submodule add https://github.com/furinasdog/MotionCapture.git Plugins/HonkaiMotionCapture
git submodule update --init --recursive
```

也可以直接 clone，目标目录保持一致：

```powershell
git clone https://github.com/furinasdog/MotionCapture.git Plugins/HonkaiMotionCapture
```

在 `.uproject` 的 `Plugins` 数组启用以下两项，不覆盖已有条目：

```json
{ "Name": "HonkaiMotionCapture", "Enabled": true },
{ "Name": "OpenCV", "Enabled": true }
```

### 2. 准备运行文件

按[安装说明](doc/INSTALLATION.md)取得固定版本 DLL 和三份通用识别权重，整理为：

```text
runtime/
  libmediapipe.dll
  Models/
    face_landmarker.task
    hand_landmarker.task
    pose_landmarker_lite.task
```

从 UE 项目根目录运行，将参数替换为实际目录：

```powershell
./Plugins/HonkaiMotionCapture/Scripts/Stage-LocalRuntime.ps1 -RuntimeSource '运行库目录'
```

脚本先校验全部源文件 SHA256，再复制到插件。它不下载文件、不安装环境、不执行输入目录中的程序。DLL 和识别权重不在 Git 仓库中；缺少它们时可编译插件，但不能启动识别。

### 3. 接入宿主

宿主 `.Build.cs` 添加 `HonkaiMotionCapture` 依赖，长期持有采集对象，在游戏线程启动并逐帧轮询：

```cpp
#include "HonkaiMotionCapture.h"

// 宿主类成员，不要在每次 Tick 中临时创建。
FHonkaiMotionCapture Capture;

// 初始化：true 表示已排队，尚不能认定摄像头可用。
FString Error;
const bool bQueued = Capture.Start(FHonkaiCaptureSettings{}, Error);

// Tick：Running 也可能未检测到任何人。
FHonkaiCaptureFrame Frame;
if (Capture.Poll(Frame) && Frame.State == EHonkaiCaptureState::Running)
{
    if (Frame.bFaceTracked)
    {
        // FaceRotationRH / BlendShapes → 坐标转换、校准和角色映射。
    }
}

// 停止或宿主 EndPlay：发出协作停止请求。
Capture.Stop();
```

以上是三个生命周期位置的片段，不是依次在一个函数中执行的完整示例。状态处理、停止与重启、坐标及预览接入见[集成说明](doc/INTEGRATION.md)。

## 文档与贡献

- [安装、固定运行库与打包](doc/INSTALLATION.md)
- [C++ 集成、输出语义与故障排查](doc/INTEGRATION.md)
- [实现原理与源码导览](doc/ARCHITECTURE.md)
- [构建与测试](doc/TESTING.md)
- [参与贡献](CONTRIBUTING.md)

问题反馈请提供 UE/插件版本、Windows 与摄像头信息、复现步骤和脱敏日志。请勿上传未经授权的模型、个人录像或相机画面。

## 许可证与运行边界

原创代码采用 [MIT](LICENSE)。MediaPipe 头文件、运行库、识别权重、OpenCV 和 Unreal Engine 各自适用原始许可或条款，详见[第三方说明](THIRD_PARTY_NOTICES.md)。本项目不会把它们重新授权为 MIT。

MediaPipe 的[官方 API 条款](https://developers.google.com/edge/mediapipe/legal/tos)说明了设备端输入处理和使用/性能指标通信。本插件没有网络沙箱或关闭 SDK 遥测的接口，不能承诺完全离线。原生 DLL 与 UE 共用进程，严重原生故障可能影响宿主；DirectShow 或 SDK 阻塞时不能保证立即停止。静态图片或内存测试不代表真人动捕和最终角色效果已验收。
