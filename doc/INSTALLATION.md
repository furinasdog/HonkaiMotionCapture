# 安装、运行库与打包

本页从插件根目录开始。宿主安装步骤见 [README](../README.md)。仓库名与内部 UE 模块名不同，clone/submodule 目标目录须为 `Plugins/HonkaiMotionCapture`。

## 构建依赖

- Windows x64、UE 5.8 C++ 项目及其要求的 MSVC/Windows SDK。
- 引擎内置 OpenCV 与 OpenCVHelper 模块；当前适配 OpenCV 4.5.5。
- PowerShell，用于本地运行文件校验和复制。

插件不需要 Python 解释器，也不启动独立识别进程。当前使用 MediaPipe 动态库，没有实现静态 MediaPipe 构建路径。运行文件缺失不妨碍源码编译，但 `Start` 会报告缺失文件。

## 获取固定版本运行文件

先阅读[第三方说明](../THIRD_PARTY_NOTICES.md)。运行库和三份通用识别权重由使用者自行取得，本插件不自动下载或安装它们。

| 文件 | 固定来源 |
| --- | --- |
| `libmediapipe.dll` | 官方 [MediaPipe 0.10.35 Windows x64 wheel](https://pypi.org/project/mediapipe/0.10.35/#files)，文件名 `mediapipe-0.10.35-py3-none-win_amd64.whl`，归档内路径 `mediapipe/tasks/c/libmediapipe.dll` |
| `face_landmarker.task` | [Face Landmarker float16/1](https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task) |
| `hand_landmarker.task` | [Hand Landmarker float16/1](https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task) |
| `pose_landmarker_lite.task` | [Pose Landmarker Lite float16/1](https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task) |

wheel 是 ZIP 格式的分发容器，可用已有 ZIP 工具提取 DLL，无须安装 wheel 或 Python。该 wheel 的 SHA256 为 `b08f001cf3c3cd0d88d9ed68f3368dc8a4913f568281a93117f083115aa672ba`。若已安装相同版本，也可复制其中匹配清单的 DLL。

DLL 与模型的完整 SHA256 以 [runtime-artifacts.json](../Scripts/runtime-artifacts.json) 为准。文件名相同不代表 ABI 或内容一致，不能用其他版本改名替代。

将文件整理到一个本地目录：

```text
runtime/
  libmediapipe.dll
  Models/
    face_landmarker.task
    hand_landmarker.task
    pose_landmarker_lite.task
```

在插件根目录执行，参数可使用绝对路径：

```powershell
./Scripts/Stage-LocalRuntime.ps1 -RuntimeSource '运行库目录'
```

脚本先检查全部源文件，再逐项复制；正确的目标文件会复用。任一源文件缺失或哈希不符都会报错，不会继续复制。

| 插件内目标路径 | 用途 |
| --- | --- |
| `Binaries/ThirdParty/Win64/libmediapipe.dll` | 动态加载的 SDK |
| `Resources/Models/face_landmarker.task` | 面部任务模型 |
| `Resources/Models/hand_landmarker.task` | 手部任务模型 |
| `Resources/Models/pose_landmarker_lite.task` | 身体姿态任务模型 |

脚本不执行这些文件，不修改防火墙、SDK 配置或摄像头权限。哈希是复制前的身份检查；`Start` 当前检查文件存在性，不会每次重新计算全部哈希。部署时应保留经校验的原文件。

## 编译和打包

宿主 `.Build.cs` 将 `HonkaiMotionCapture` 加入依赖列表；宿主公开头文件暴露插件类型时用公开依赖，否则可用私有依赖。随后使用正常的 UE 项目构建流程。没有 C++ 模块的项目需先建立 C++ 宿主，或自行准备匹配引擎版本的预编译插件。

`HonkaiMotionCapture.Build.cs` 将存在的 DLL 和三份模型声明为 NonUFS 运行依赖。**准备完成运行文件后再打包**；插件编译成功不能证明输出包包含识别依赖。发行前在输出插件目录检查四个文件及哈希，再从独立输出目录启动测试。

OpenCV 来自引擎模块，不可随意拷入其他 OpenCV DLL 混用。UE、OpenCV 或架构变化后重新核对 ABI。摄像头访问受系统权限、设备占用和安全软件策略影响，插件不修改这些全局设置。

发行方需要保留实际 SDK、模型、OpenCV 和 UE 的适用许可与声明。MIT 不授予这些依赖额外的分发权。
