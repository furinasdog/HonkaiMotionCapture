# HonkaiMotionCapture

独立的 UE 原生动捕插件，面向 Windows x64。通过进程内 MediaPipe 0.10.35 C API 计算面部、上半身和双手；使用引擎已安装的 OpenCV 模块采集。插件不依赖 HonkaiRendering，不启动外部 EXE，不使用 `frame.json` 或其他文件 IPC，也不包含 PMX 解析与骨骼重定向。

## 准备与构建

将插件放入任一 UE 5.8 项目的 `Plugins/HonkaiMotionCapture`，启用插件和引擎 OpenCV 插件。当前源码基于本机 UE 5.8 的 OpenCV 4.5.5 模块，其他引擎版本尚未验证。

已有的本地运行库目录应包含 `libmediapipe.dll` 和 `Models/` 下的三个 `.task` 文件。运行：

```powershell
./Scripts/Stage-LocalRuntime.ps1 -RuntimeSource '已有原生运行库/bin目录'
```

脚本按 `Scripts/runtime-artifacts.json` 校验 SHA256，验证全部源文件后复制到本插件。它不下载、不执行输入目录中的程序，也不改变防火墙、代理、摄像头隐私或 SDK 全局设置。DLL 和模型均不提交到 Git。运行文件尚未准备时，插件可以编译，`Start` 返回明确错误。

现有 Windows SDK 发行包提供 DLL，本机没有 Bazel/Bazelisk，本轮使用动态加载后端，没有把 import library 当作静态实现。静态源码构建并非不可实现：官方同版本 `tasks/c/BUILD` 的相关目标使用 `cc_binary(linkshared = True)`，真正静态构建需要独立处理该版本的构建目标和依赖；本仓尚未实现或验证这条路径。

## C++ 接口

宿主模块依赖 `HonkaiMotionCapture`，包含 `HonkaiMotionCapture.h`：

```cpp
FHonkaiMotionCapture Capture;
FString Error;
Capture.Start(FHonkaiCaptureSettings{}, Error);

FHonkaiCaptureFrame Frame;
if (Capture.Poll(Frame))
{
    // 检查 State。Running 帧中的 bFaceTracked/bPoseTracked 可为 false。
    // FaceRotationRH 和关键点仍为原始 MediaPipe 坐标。
}
Capture.Stop();
```

`Start`、`Stop`、`Poll` 在宿主游戏线程调用。`Start` 返回 true 表示工作已排队，SDK 与摄像头是否成功仍需读取 `Poll` 的 Starting/Running/Error 状态。采集对象应作为长期成员保存并在 Tick 中轮询。推理和摄像头操作由一个后台线程独占；游戏线程只复制最新完整快照。序号、时间戳在同一采集对象的重启之间仍单调递增，未出现新快照时 `Poll` 返回 false。面部、身体与手部均来自同一采集帧，没有将未检测的默认值当成有效追踪。

快照包括 SDK 原始右手相机四元数、52 个面部通道、33 个 pose/image 与 world 关键点、左右手各 21 个 image/world 关键点。SDK 未检测到某部位时对应数组为空。插件不镜像、不修改动作方向、不校准中立姿态；宿主应统一做坐标转换、镜像、可见度判断、校准和模型映射，避免头部与身体采用不同转换。

`InferenceMilliseconds` 仅计 SDK 调用，`ProcessingFPS` 是实际成功处理帧之间的测量值，并非请求的摄像头帧率，也不是端到端显示延迟。

宿主可调用 `SetPreviewEnabled(true)`，按需取得同一摄像头帧的 `PreviewWidth`、`PreviewHeight`、紧密排列的 `PreviewBGRA` 和 `FaceLandmarks`。预览最长宽320、高240，保持比例；它复用采集线程，不打开第二个摄像头、不保存图像文件。图像和关键点仍未镜像，宿主应对两者只做一次相同的镜像。关闭预览会清除图像与面点，并丢弃晚到预览；动捕继续运行。停止采集会一并清除预览。预览不是额外的录制或输出功能。

## 生命周期与当前边界

`Stop` 异步请求协作停止并立即清除有效追踪。`IsStopping` 会保持到工作线程释放摄像头和 SDK；停止未完成时拒绝重复启动。插件不会强杀 UE 线程。销毁采集对象会请求停止，模块保留工作线程直到退出；模块卸载时等待所有线程结束。

OpenCV 运行库由官方 `OpenCVHelper` 模块持有。启动前只使用 `GetModuleHandleW` 检查模块是否已加载，不增加引用、不释放借用句柄。UE Windows 的 `GetDllHandle` 对已加载模块可能返回借用句柄，不能把该结果当作独立引用释放，否则重复启动时缓存的延迟导入地址可能失效。

OpenCV DirectShow 的读取/释放和 SDK 关闭可能受驱动或 SDK 阻塞影响，当前不能保证有界停止时间。模块卸载可能因此等待，不能将这种等待称为已解决。进程内原生 DLL 的严重故障也可能影响 UE；原先外部进程的崩溃隔离不再存在。

官方预编译 MediaPipe SDK 包含 usage/performance telemetry，官方没有提供关闭 API。此插件在 UE 进程内运行，**不再有 AppContainer 网络隔离，不声称完全离线或绝不联网**；插件本身没有另加图像上传接口。相关事实和限制见 [上游 issue 6291](https://github.com/google-ai-edge/mediapipe/issues/6291) 与 [MediaPipe API 条款](https://developers.google.com/edge/mediapipe/legal/tos)。

## 验证

UE 自动化 `HonkaiMotionCapture.InMemorySnapshots` 检查完整快照、状态、单调序号/时间、重复读取及无效参数，不会打开摄像头。`Tests/NativeSdkSmoke.cpp` 通过同一原生 C API 核心对静态图片执行 VIDEO 模式推理，不通过文件交换快照；静态图片验证不等于真人摄像头验收。

独立 fixture 可使用已安装的 CMake/MSVC 编译，不需要 UE 或 OpenCV：

```powershell
cmake -S Tests -B Tests/build -A x64 -DMEDIAPIPE_RUNTIME_DIR='原生运行库/bin目录' -DFIXTURE_IMAGE='本地测试图片路径'
cmake --build Tests/build --config Release
ctest --test-dir Tests/build -C Release --output-on-failure
```

只验证真实 OpenCV 模块的预检查生命周期，不启动摄像头、MediaPipe 或 UE：

```powershell
cmake -S Tests -B Tests/build -A x64 -DOPENCV_RUNTIME_DLL='引擎插件内opencv_world455.dll完整路径'
cmake --build Tests/build --config Release --target RuntimeModuleLiveness
ctest --test-dir Tests/build -C Release -R OpenCvRuntimeModuleLiveness --output-on-failure
```

该测试直接编译生产预检查函数，连续三轮加载/卸载实际 OpenCV DLL；每轮2000次检查验证构造函数代码所在模块持续存活，并验证最后一个拥有者释放后模块能卸载。测试不调用 `VideoCapture` 构造或相机接口，不使用 PIN，也不以快照轮询替代模块存活验证。

独立 C++ fixture 已完成两轮 SDK 初始化、每轮 3 个递增 VIDEO 帧和关闭：均检测到面部 52 通道、双手各 21 个 image/world 点，姿态在每轮 3 帧中有 2 帧检出 33 个 image/world 点，未检出的帧保持为空。本机重复试验观察到初始化约 150–200 ms、推理约 28–65 ms、关闭约 27–75 ms，均为这张静态测试图片的结果。SDK stderr 包含 clearcut 连接尝试失败，不能据此声称插件实施了网络隔离。

本机 UE 5.8.4 Development Editor 编译及 `HonkaiMotionCapture.InMemorySnapshots` 自动化已通过；宿主的输入转换测试也已通过。真实摄像头帧率、关闭/重开、长时间运行和打包分发需分别验证。

## 许可证

本插件代码使用 MIT。MediaPipe C API 头文件保持 v0.10.35 原始 Apache 2.0 声明，OpenCV 的 Apache 2.0 及附带第三方声明单独保留。MIT 不替代上游 SDK、模型或 API 条款。详见 `THIRD_PARTY_NOTICES.md`。
