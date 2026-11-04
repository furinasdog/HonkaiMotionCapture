# C++ 集成与输出语义

公开接口位于 [HonkaiMotionCapture.h](../Source/HonkaiMotionCapture/Public/HonkaiMotionCapture.h)。行为以当前源码为准；角色骨骼和表情映射由宿主完成。

## 生命周期

宿主长期持有 `FHonkaiMotionCapture`，在游戏线程调用 `Start`、`Poll`、`Stop` 和预览设置。不要每帧创建对象或重复启动，对象也不可复制。

| 操作或状态 | 含义 |
| --- | --- |
| `Start(Settings, Error) == false` | 参数、依赖、已有工作线程或线程创建检查失败；读取 `Error`。并非所有早期失败都会产生新的 Error 状态快照 |
| `Start(...) == true` | 已排队初始化，尚未证明 SDK 或摄像头打开成功 |
| `Starting` | 后台初始化中，等待后续快照 |
| `Running` | 管线运行；用 `bFaceTracked`、`bPoseTracked`、`Hands` 分别判断有效识别 |
| `Stopping` | 已请求停止，旧追踪和预览从当前快照清除，等待资源释放 |
| `Stopped` | 工作线程结束，可再次启动 |
| `Error` | 后台初始化、采集或推理失败；读取快照 `Error` 与 `Status` |

`IsRunning()` 判断工作线程是否尚未结束，在 Starting 和 Stopping 阶段也可能为 true，不能替代 `Frame.State == Running`。区分正在停止时使用 `IsStopping()`。

`Poll` 只返回最新完整快照。无新序号时返回 false，不应把此前的 `Frame` 当成新识别。慢于识别频率的宿主会跳过中间帧，不积累无界队列；状态变化也产生快照，过渡状态可能在下一次轮询前被覆盖。

### Tick 中处理状态

```cpp
FHonkaiCaptureFrame Frame;
if (Capture.Poll(Frame))
{
    switch (Frame.State)
    {
    case EHonkaiCaptureState::Running:
        if (Frame.bFaceTracked)
        {
            // 转换 FaceRotationRH，映射 BlendShapes。
        }
        if (Frame.bPoseTracked)
        {
            // 检查单点可见性，再用关键点计算躯干与四肢方向。
        }
        // Hands 可为空，不用上一帧数组推断本帧仍有手。
        break;
    case EHonkaiCaptureState::Error:
        // 显示 Frame.Error，停止驱动旧结果；不要逐帧自动重试。
        break;
    default:
        // 显示 Frame.Status，按宿主策略保持、回中或停止驱动。
        break;
    }
}
```

宿主还应设置结果过期时间：无新帧、设备断开或失追时，不无限保持“正在追踪”。短暂丢点保持、回中、死区与滤波属于宿主策略，插件不替调用方决定。

### 停止与重启

`Stop()` 发出协作停止请求后立即返回，不在游戏线程同步关闭相机。后台停止后再调用 `Start`；停止期间的新启动会被拒绝，不要忙等待阻塞游戏线程。

对象析构会请求停止。模块持有工作线程直至相机和 SDK 释放，模块卸载时等待全部工作线程结束。DirectShow 读帧、释放或 SDK 关闭可能阻塞，不能保证停止/卸载时长。插件禁用动态重载，不应强制卸载持有相机的模块。

## 参数

| 字段 | 默认值 | 范围或说明 |
| --- | --- | --- |
| `CameraIndex` | 0 | 0–32，DirectShow 索引；插件无设备枚举 API |
| `Width` | 640 | 160–3840，向驱动请求 |
| `Height` | 480 | 120–2160，向驱动请求 |
| `FramesPerSecond` | 30 | 1–60，用于设备请求和后台节奏控制，不代表实际 FPS |
| `Confidence` | 0.5 | 有限值，`0 < value <= 1`；设置三类任务的 detection/presence/tracking 阈值 |

驱动未必接受请求参数；快照 `Width`、`Height` 为实际图像尺寸。插件请求 MJPG，最终格式和帧率由设备协商决定。DirectShow 索引不保证与其他枚举后端顺序相同。

## 快照字段

| 输出 | 语义 |
| --- | --- |
| `Sequence` | 每次发布快照递增，包含状态变化，不等于相机帧号 |
| `TimestampSeconds` | UE 单调时钟的秒值；识别帧取采集时刻，同一对象重启后仍递增；不是 Unix 时间 |
| `FaceRotationRH` | SDK 面部矩阵提取的归一化右手相机空间四元数，不是 UE 骨骼旋转 |
| `BlendShapes` | 有效人脸的 52 个命名通道，限制到 `[0,1]`；未检测到脸时为空 |
| `PoseLandmarks` / `PoseWorldLandmarks` | 有效姿态各 33 点，遵循 MediaPipe 顺序，无姿态时为空 |
| `Hands` | 最多两只手，含 `Handedness`、分类 `Score` 和各 21 点图像/世界坐标 |
| `InferenceMilliseconds` | SDK 三类推理及结果读取耗时，不含相机读帧、预览或最终渲染，不是端到端延迟 |
| `ProcessingFPS` | 实际成功推理完成间隔的平滑统计，首帧或未形成统计时可为 0 |

归一化图像坐标用于画面定位，不能直接当作 UE 世界厘米。世界关键点保留 SDK 空间语义；身体与手部有各自参考原点，不是同一 UE 世界中的绝对位置。参考官方[姿态输出](https://developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker)和[手部输出](https://developers.google.com/edge/mediapipe/solutions/vision/hand_landmarker)。

`FHonkaiCaptureLandmark` 保留 `Visibility`、`Presence`；SDK 未提供对应字段时写 0。因此不能要求全部手/脸关键点的这两项均大于阈值。先判断部位是否被检测到，再按任务的输出语义判断点可靠性。`Hands[].Score` 是左右手分类分数，不是每个关节点的可见性。

## 坐标、镜像与绑定

插件不镜像图像，不标定中立姿态，不转换到 UE。宿主需建立一致的右手相机→UE/模型基底，并在头、身体、手部使用同一约定。

只反转一个欧拉角分量不能普遍替代基底转换。镜像涉及位置、旋转和左右语义，不只是给头部角度乘负号。将镜像作为宿主的一次明确转换，避免重复反转。

预览镜像与角色驱动镜像可以是不同的产品选项；画覆盖层时，对图像及对应图像关键点使用相同变换。左右手名称来自未镜像输入上的 SDK 分类，不仅凭“画面左侧”改为被摄者左手。

识别数据不能自动保证任意模型的闭眼、口型或骨骼正确。宿主需要中立姿态标定、限幅、竞争形变混合和丢点处理，并分别验收采集、映射、物理与渲染。

## 按需预览

`Capture.SetPreviewEnabled(true)` 后，成功帧可包含 `PreviewWidth`、`PreviewHeight`、`PreviewBGRA` 和 `FaceLandmarks`。图像紧密排列为 BGRA，每像素 4 字节，无行尾填充；保持比例，最大 320 × 240，不放大小输入。

预览默认关闭。关闭时清除图像与面点，丢弃晚到预览；姿态、表情和手部识别继续。`FaceLandmarks` 只随预览输出，预览关闭或可选面点读取无效时为空，不应单独用它判定人脸失追。停止采集也清除预览。

预览复用同一帧，不打开第二个摄像头、不保存图像。宿主自行上传 BGRA 纹理及绘制关键点；结果面板不可见时可关闭复制。

## 故障排查

| 现象 | 检查项 |
| --- | --- |
| `Start` 立即失败 | 读取 `Error`；检查线程、参数、运行文件、OpenCV 及上轮停止状态 |
| Starting 后 Error | 检查 DLL 导出/依赖、模型版本、摄像头索引、权限或设备占用 |
| Running 无人体 | 检查实际画面、光照、遮挡和阈值；这是合法结果，不等于启动失败 |
| 无预览或面点 | 检查预览开关、纹理上传和新快照，不据此认定无表情数据 |
| 方向反转或身体扭曲 | 检查宿主基底、中立姿态、父骨骼转换与重复镜像，不按角色修补采集层 |
| FPS 低 | 比较实际尺寸和 `ProcessingFPS`，请求 FPS 不是读帧/推理速率 |
| 停止慢或重启被拒绝 | 等待停止完成，排查驱动/SDK 阻塞，不强杀线程 |

进程内 SDK 无崩溃隔离；C++ 错误处理不能保证捕获全部原生故障。网络和许可边界见[第三方说明](../THIRD_PARTY_NOTICES.md)。
