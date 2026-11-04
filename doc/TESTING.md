# 构建与测试

内存协议、原生库、真实设备与宿主效果分别验收，不把一层通过写成整条动捕链路通过。本页提供可复现入口，不保存某台机器的阶段日志或硬件性能承诺。

## UE 宿主构建

按[安装说明](INSTALLATION.md)接入 C++ 宿主，使用匹配的 UE/MSVC/Windows SDK 构建 Editor 目标。准备运行文件后再进行设备测试与打包。已运行的编辑器可能持有旧 DLL，不在采集中强制替换或动态卸载模块。

在 UE 自动化测试窗口运行：

```text
HonkaiMotionCapture.InMemorySnapshots
```

也可从已有宿主使用对应引擎的命令行编辑器：

```text
UnrealEditor-Cmd.exe <HostProject.uproject> -unattended -nop4 -nullrhi -ExecCmds="Automation RunTests HonkaiMotionCapture.InMemorySnapshots" -TestExit="Automation Test Queue Empty"
```

替换项目路径并检查自动化报告与错误，不能只凭进程退出码判定通过。测试覆盖完整快照、最新帧语义、递增序号/时间戳、预览关闭丢弃晚到图像、停止后丢弃推理、重复停止和无效参数，不打开摄像头、不执行 SDK 推理。

## 独立 C++ 测试

需要已有 Windows x64 MSVC、Windows SDK 与 CMake 3.20 或更新版本。在插件根目录执行，生成物位于忽略的 `Tests/build/`：

```powershell
cmake -S Tests -B Tests/build -A x64
cmake --build Tests/build --config Release
ctest --test-dir Tests/build -C Release --output-on-failure
```

全新构建目录未配置可选参数时，CTest 只注册 `InMemoryPreviewPixels`；复用目录会保留以前的 CMake 参数。该测试检查生产像素复制函数的通道顺序、行填充、缓冲长度、尺寸和溢出防护，不启动相机或 SDK。

### OpenCV 模块所有权

```powershell
cmake -S Tests -B Tests/build -A x64 -DOPENCV_RUNTIME_DLL='引擎所用 OpenCV DLL 完整路径'
cmake --build Tests/build --config Release --target RuntimeModuleLiveness
ctest --test-dir Tests/build -C Release -R OpenCvRuntimeModuleLiveness --output-on-failure
```

`RuntimeModuleLiveness` 编译生产预检查函数，反复检查实际 DLL 存活，并验证最后一个拥有者释放后可以卸载。它不构造 `VideoCapture`、不接触摄像头，不能证明相机启动或重开正确。

### MediaPipe 原生推理

```powershell
cmake -S Tests -B Tests/build -A x64 -DMEDIAPIPE_RUNTIME_DIR='含 DLL 与 Models 的本地运行库目录' -DFIXTURE_IMAGE='已获授权的测试图片完整路径'
cmake --build Tests/build --config Release --target NativeSdkSmoke
ctest --test-dir Tests/build -C Release -R NativeSdkInMemorySmoke --output-on-failure
```

fixture 需要 RGB 图片，并要求包含可识别人脸、两只手与身体姿态；任意照片不一定满足测试断言。SDK 图像 API 解码输入，执行两轮初始化、每轮三个递增 VIDEO 帧和关闭，检查带行填充图像、无效 DLL、重复时间戳、52 通道、双手及姿态输出。

运行库布局与[安装说明](INSTALLATION.md)一致。测试不需要 UE 或 OpenCV、不打开摄像头，但会加载真实 MediaPipe，其[网络及许可边界](../THIRD_PARTY_NOTICES.md)仍适用。静态图片重复推理不能证明真人运动、长时间稳定性或端到端延迟。

## 真实设备与发行验收

发布前在实际输出目录和干净的宿主用户配置下检查：

- 四个运行文件完整、哈希正确；匹配的引擎/OpenCV 依赖已打包。
- 排队启动后的实际 Running/Error 状态；被占用、不存在或无权限的相机有明确错误。
- 脸、上半身、左右手分别进入/离开画面，未检出时不驱动旧数据。
- 预览开关、镜像及关键点与原始通道一致。
- 停止、完成后重启、关闭宿主和不同相机的资源释放。
- 实测尺寸、识别 FPS、推理耗时，按需测量端到端延迟，不用请求 FPS 代替。
- 宿主中立姿态、头身同向、表情混合及物理表现作为独立验收项目。

记录环境、步骤、结果和未验证范围。可保留本地私有证据，不向仓库提交人物录像、角色素材或完整机器路径。设备支持或生命周期变化后重新执行相关硬件检查。
