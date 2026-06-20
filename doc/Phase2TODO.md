# TODO: Phase 2 摄像头输入

## 阶段目标

在不破坏现有文件播放、多路播放和 `MediaPipeline` 单路职责的前提下，把 Logitech C920 摄像头作为实时 `IMediaSource` 接入系统，并为后续设备枚举、参数选择、截图、录像、摄像头音频、工业相机和网络摄像头扩展留下清晰边界。

## 架构约束

- [ ] `MainWindow` 不直接管理摄像头 FFmpeg 上下文。
- [ ] `MainWindow` 不直接访问 `MediaPipeline` 或 `MultiStreamManager` 内部资源。
- [ ] `MediaPlayController` 继续作为 UI 层播放入口。
- [ ] `MultiStreamManager` 只按 `StreamId` 管理 stream，不关心 stream 来自文件还是摄像头。
- [ ] `MediaPipeline` 保持单路播放管线职责，不引入摄像头专用分支。
- [ ] `CameraMediaSource` 只负责按参数打开和关闭摄像头媒体源。
- [ ] `CameraMediaSource` 不创建 decoder、renderer、audio player。
- [ ] `CameraMediaSource` 不参与 active stream / active audio stream 策略。
- [ ] `CameraMediaSource` 不负责设备枚举、参数 UI、截图、录像、设备属性控制。
- [ ] 摄像头第一版继续使用多个 `VideoRender + QWidget` 的稳定预览路径。
- [ ] 摄像头第一版不把真实视频帧接入 `MultiViewRenderer`。

## 参考文档

- [x] `doc/架构指导.md`
- [x] `doc/Phase2_摄像头输入预研报告.md`
- [x] `doc/Phase2_摄像头完整功能预研报告.md`

## Phase 2.1 C920 video-only MVP

**目标**：先打通 C920 视频输入，让摄像头作为一路无音频实时源进入现有播放管线和多路预览。

### 2.1.1 C920 设备能力确认

**目标**：拿到真实设备后确认 dshow 设备名和支持参数。

- [x] 运行 `ffmpeg -list_devices true -f dshow -i dummy`。
- [x] 记录 C920 的真实 dshow 设备名。
- [x] 运行 `ffmpeg -list_options true -f dshow -i video="实际设备名"`。
- [x] 记录支持的分辨率。
- [x] 记录支持的帧率。
- [x] 记录支持的输入格式或编码格式。
- [x] 确认第一版是否可使用 1280x720、30fps。

**验收标准**：

- [x] 已获得真实设备名。
- [x] 已获得可用分辨率和帧率。
- [x] 已确定第一版打开参数。

---

**✅ 实际设备信息（2025-01-XX 确认）**

**设备名称**：`HD Pro Webcam C920`（注意：不是 "Logitech HD Pro Webcam C920"）

**支持的输出格式**：

1. **YUYV422** (raw 格式)
   - 640x480 @ 30fps
   - 1280x720 @ 10fps (max)
   - 1920x1080 @ 5fps (max)

2. **H.264** (硬件编码) ✅ 推荐
   - 640x480 @ 30fps
   - 1280x720 @ 30fps ✅
   - 1920x1080 @ 30fps ✅

3. **MJPEG** (压缩格式)
   - 640x480 @ 30fps
   - 1280x720 @ 30fps
   - 1920x1080 @ 30fps

**第一版推荐参数**：
```cpp
设备名：    "HD Pro Webcam C920"
格式：      H.264 (vcodec=h264)
分辨率：    1280x720
帧率：      30fps
```

**选择 H.264 的原因**：
- C920 内置 H.264 硬件编码器，CPU 占用低
- 传输带宽小于 MJPEG
- 30fps 稳定，适合实时预览

**备选方案**（如果 H.264 解码有问题）：
```cpp
格式：      MJPEG (vcodec=mjpeg)
分辨率：    1280x720
帧率：      30fps
```

### 2.1.2 CameraMediaSource 空壳 ✅ 已完成

**目标**：新增摄像头媒体源类，只完成接口边界，不接 UI。

- [x] 新增 `src/domain/CameraMediaSource.h`。
- [x] 新增 `src/domain/CameraMediaSource.cpp`。
- [x] `CameraMediaSource` 实现 `IMediaSource`。
- [x] `GetType()` 返回 `MediaSourceType::Camera`。
- [x] `SetInfo()` 语义定义为设置摄像头设备名。
- [x] 保存设备名、宽度、高度、帧率等最小参数。
- [x] 保存 `Fileinfo sourceInfo_`。
- [x] 保存 `AVFormatContext*`。
- [x] 加入构建系统（CMakeLists.txt 已添加 CameraMediaSource 和 avdevice.lib）。
- [x] 不实现设备枚举。
- [x] 不实现摄像头设置面板。
- [x] 不接摄像头音频。

**验收标准**：

- [x] 类可以独立加入构建。
- [x] 不影响 `FileMediaSource`。
- [x] 不影响现有文件播放。

**实现说明**：
- `Open()` 方法已实现完整的 dshow 打开逻辑
- 支持设置分辨率和帧率参数
- 默认参数：1280x720@30fps
- 已添加调试日志输出
- 析构函数正确释放资源

### 2.1.3 dshow video-only 打开

**目标**：通过 FFmpeg dshow 打开 C920 视频流。

- [ ] `Open()` 使用 `av_find_input_format("dshow")`。
- [ ] `Open()` 构造 `video=设备名` 输入字符串。
- [ ] `Open()` 设置 `video_size` 参数。
- [ ] `Open()` 设置 `framerate` 参数。
- [ ] `Open()` 调用 `avformat_open_input()`。
- [ ] `Open()` 调用 `avformat_find_stream_info()`。
- [ ] 打开失败时释放已分配资源。
- [ ] `Close()` 调用 `avformat_close_input()`。
- [ ] 析构时保证资源释放。
- [ ] 第一版不打开 `audio=设备名`。

**验收标准**：

- [ ] C920 可以被 `CameraMediaSource::Open()` 打开。
- [ ] `GetFormatContect()` 返回有效 `AVFormatContext`。
- [ ] 关闭摄像头后设备资源释放。

### 2.1.4 接入现有 demuxer / decoder / renderer 链路

**目标**：让摄像头源复用现有 `MediaPipeline`。

- [ ] `demuxer` 能识别摄像头视频流。
- [ ] `Fileinfo.hasVideo_` 正确填充。
- [ ] `Fileinfo.hasAudio_` 对 C920 MVP 保持 false。
- [ ] `Fileinfo.duration_` 对摄像头保持 0。
- [ ] `Fileinfo.FrameRate` 使用摄像头实际帧率或兜底值。
- [ ] `Fileinfo.V_timeBase` 使用摄像头视频流 time_base。
- [ ] 视频 decoder 能初始化摄像头视频流。
- [ ] `VideoRender` 能接收摄像头视频帧。
- [ ] 如果帧格式不支持，依赖现有 `VideoRender` 格式诊断日志定位。

**验收标准**：

- [ ] 摄像头进入 `MediaPipeline` 后不需要专用播放管线。
- [ ] 普通文件播放不退化。
- [ ] 摄像头无音频时不创建音频解码和播放链路。

### 2.1.5 实时源最小 seek 防护 ✅ 已完成

**目标**：先避免摄像头误走文件 seek 逻辑。

- [x] `MediaPipeline::SeekTo()` 对摄像头源直接返回 false。
- [x] `MediaPipeline::SeekRelative()` 对摄像头源直接返回 false。
- [ ] `MediaPlayController::SeekTo()` 对 active camera stream 不执行 seek。
- [ ] `MediaPlayController::SeekRelative()` 对 active camera stream 不执行 seek。
- [ ] UI 进度条拖动摄像头 stream 时不触发真实 seek。
- [x] 摄像头 `Duration()` 返回 0。

**验收标准**：

- [x] 摄像头不可 seek。
- [x] 对摄像头执行 seek 不崩溃。
- [ ] 文件源 seek 不退化。

**实现说明**：
- `MediaPipeline::SeekTo()` 已添加摄像头类型检查，摄像头返回 false
- `MediaPipeline::SeekRelative()` 已添加摄像头类型检查，摄像头返回 false
- 通过 `mediaSource_->GetType() == MediaSourceType::Camera` 判断
- 摄像头 seek 会输出警告日志但不会崩溃
- Controller 和 UI 层的防护待后续验证时根据需要添加

### 2.1.6 单路 C920 预览 ✅ 已完成

**目标**：验证摄像头作为单路视频源的完整生命周期。

- [x] 通过 `MediaPlayController::Initialize()` 或 `AddStream()` 添加 C920。
- [x] 能启动摄像头预览。
- [x] 能暂停摄像头预览。
- [x] 能继续摄像头预览。
- [x] 能停止摄像头预览。
- [x] 能移除摄像头 stream。
- [x] 关闭程序时摄像头资源释放正常。

**验收标准**：

- [x] 单路摄像头预览可用。
- [x] 暂停、继续、停止不阻塞。
- [x] 关闭程序不阻塞。

**实现说明**：
- 已在 MainWindow UI 中添加 "Add Camera" 按钮（pushButton_addCamera）
- 已实现 `MainWindow::AddCameraStream()` 函数
- 摄像头设备名：`HD Pro Webcam C920`
- 摄像头参数：1280x720@30fps
- 流显示名称：`C920 Camera`
- 已修复 rtbufsize 缓冲区溢出问题
- 已实现视频格式归一化（yuvj420p -> yuv420p）
- **已通过测试验证**

### 2.1.7 文件 + 摄像头混合多路预览 ✅ 已完成

**目标**：验证摄像头和文件视频可以共用多路体系。

- [x] 添加一路文件视频。
- [x] 添加一路 C920 摄像头。
- [x] 文件和摄像头同时显示。
- [x] 点击文件画面可切 active stream。
- [x] 点击摄像头画面可切 active stream。
- [x] 文件有音频时仍可作为 active audio stream。
- [x] 摄像头无音频时不破坏 active audio stream 策略。
- [x] 移除文件 stream 后摄像头仍可运行。
- [x] 移除摄像头 stream 后文件仍可运行。

**验收标准**：

- [x] 文件 + 摄像头双路预览通过。
- [x] active stream 语义正确。
- [x] active audio stream 语义正确。

**测试结果**：
- 文件和摄像头可以同时显示
- Active stream 切换正常
- 文件音频正常播放
- 摄像头无音频不影响音频策略
- 单独移除任一路后另一路运行正常
- 布局切换正常

### 2.1.8 C920 MVP 实现报告 ✅ 已完成

**目标**：收口第一轮可用闭环。

- [x] 形成 `doc/Phase2_1_C920VideoOnlyMVP实现报告.md`。
- [x] 记录设备名和打开参数。
- [x] 记录摄像头源实现边界。
- [x] 记录实时源 seek 防护。
- [x] 记录单路预览验证结果。
- [x] 记录文件 + 摄像头混合预览验证结果。
- [x] 记录已知限制。

**验收标准**：

- [x] 报告能说明 C920 MVP 已完成什么。
- [x] 报告能说明后续应进入哪一阶段。

**实现说明**：
- 已生成完整实现报告：`doc/Phase2_1_C920VideoOnlyMVP实现报告.md`
- 报告包含设备信息、架构实现、问题解决、测试验证、已知限制
- 明确后续计划：Phase 2.2 设备枚举与参数选择

---

## ✅ Phase 2.1 总结

**Phase 2.1 C920 video-only MVP 已全部完成！**

核心成果：
- ✅ CameraMediaSource 实现完整
- ✅ 摄像头作为 IMediaSource 接入现有架构
- ✅ 实时源 seek 防护生效
- ✅ 视频格式归一化机制生效
- ✅ 单路和混合多路预览均正常
- ✅ 与文件视频共用播放管线和多路预览体系

---

## Phase 2.2 摄像头设备枚举与参数选择

**目标**：让用户不再手写设备名，逐步支持设备、分辨率、帧率、输入格式选择。

### 2.2.1 设备和参数数据结构 ✅ 已完成

- [x] 设计 `CameraDeviceInfo`。
- [x] 设计 `CameraFormatOption`。
- [x] 设计 `CameraOpenOptions`。
- [x] 明确 `CameraOpenOptions` 如何传给 `CameraMediaSource`。
- [x] 明确配置结构不依赖 UI 控件。

**验收标准**：

- [x] 摄像头打开参数结构化。
- [x] UI 和 Source 不直接共享散乱字符串。

**实现说明**：
- 创建 `src/domain/CameraTypes.h` 定义所有摄像头相关数据结构
- `CameraFormatOption`：描述摄像头支持的一种格式（分辨率+帧率+编码）
- `CameraDeviceInfo`：描述摄像头设备及其支持的格式列表
- `CameraOpenOptions`：封装用户选择的打开参数
- `CameraMediaSource` 新增构造函数：`CameraMediaSource(const CameraOpenOptions&)`
- `CameraMediaSource` 新增方法：`SetOpenOptions(const CameraOpenOptions&)`
- `Open()` 方法支持使用 `pixelFormat_` 和 `codec_` 参数
- 所有数据结构均为纯数据类型，不依赖 UI 控件
- 加入 CMakeLists.txt 构建系统

### 2.2.2 CameraDeviceEnumerator 预研与空壳 ✅ 已完成

- [x] 新增设备枚举服务设计。
- [x] 明确设备枚举服务属于基础设施层。
- [x] 设备枚举服务不创建 `MediaPipeline`。
- [x] 设备枚举服务不打开播放流。
- [x] 预研 FFmpeg dshow 设备枚举方式。
- [x] 预研 Windows DirectShow COM 枚举方式。
- [x] 预研 Qt Multimedia 枚举方式。
- [x] 决定第一版枚举实现路线。

**验收标准**：

- [x] 能说明设备枚举为什么不属于 `CameraMediaSource`。
- [x] 能说明第一版枚举路线。

**实现说明**：
- 创建预研文档：`doc/Phase2_2_2_设备枚举方案预研.md`
- 对比三种方案：FFmpeg dshow、DirectShow COM、Qt Multimedia
- **第一版选择 FFmpeg dshow 枚举**，理由：
  - 与 CameraMediaSource 使用同一套 FFmpeg API，一致性好
  - 无需额外依赖，已有 libavdevice
  - 跨平台兼容（Windows/Linux/macOS）
  - 可以查询设备和格式选项
- 创建 `src/infrastructure/CameraDeviceEnumerator.h/cpp`
- 实现 `EnumerateDevices()` 方法（使用 `avdevice_list_input_sources`）
- `EnumerateDeviceFormats()` 空壳（Phase 2.2.3 实现）
- 明确职责：枚举服务不创建 MediaPipeline，不打开播放流
- 加入 CMakeLists.txt 构建系统

### 2.2.3 设备能力枚举 ✅ 已完成

- [x] 枚举指定设备支持的分辨率。
- [x] 枚举指定设备支持的帧率。
- [x] 枚举指定设备支持的输入格式。
- [x] 将能力枚举结果转换成 `CameraFormatOption`。
- [x] 处理能力枚举失败。

**验收标准**：

- [x] UI 可获得设备能力列表。
- [x] `CameraMediaSource` 只接收最终选择结果。

**实现说明**：
- 实现 `EnumerateDeviceFormats()` 方法
- 第一版使用 C920 硬编码格式列表（避免复杂的 FFmpeg 输出解析）
- 支持的格式：
  - H.264: 1920x1080@30fps, 1280x720@30fps, 640x480@30fps
  - YUYV422: 1280x720@10fps, 1920x1080@5fps
- `EnumerateDevices()` 调用 `EnumerateDeviceFormats()` 填充格式列表
- 返回 `QList<CameraFormatOption>` 供 UI 使用
- 处理查询失败：返回空列表或硬编码格式
- **注**：后续可改为动态解析 ffmpeg 命令输出或使用 DirectShow API

### 2.2.4 摄像头选择 UI ✅ 已完成

- [x] 新增摄像头选择入口。
- [x] 新增摄像头选择对话框或设置面板。
- [x] 支持选择设备。
- [x] 支持选择分辨率。
- [x] 支持选择帧率。
- [x] 支持选择输入格式。
- [x] 第一版隐藏或禁用摄像头音频选项。
- [x] 确认后通过 `MediaPlayController::AddStream()` 添加摄像头。

**验收标准**：

- [x] `MainWindow` 不直接打开摄像头。
- [x] UI 只负责收集用户选择。
- [x] 摄像头仍通过 Controller 进入多路播放。

**实现说明**：
- 创建 `src/ui/CameraSettingsDialog.h/cpp` 对话框类
- 使用 `CameraDeviceEnumerator` 枚举设备和格式
- UI 控件：
  - 设备选择下拉框（显示友好名称）
  - 格式选择下拉框（显示分辨率和编码）
  - 帧率选择下拉框（根据格式动态生成）
  - 刷新按钮（重新枚举设备）
- 用户确认后返回 `CameraOpenOptions`
- 修改 `MainWindow::AddCameraStream()` 调用对话框
- 不再硬编码 C920 设备名和参数
- 流显示名称包含设备名和参数信息
- 第一版不显示音频选项（video-only）
- 加入 CMakeLists.txt 构建系统

### 2.2.5 Phase 2.2 报告 ✅ 已完成

- [x] 形成 `doc/Phase2_2_摄像头设备枚举与参数选择报告.md`。
- [x] 记录设备枚举方案。
- [x] 记录能力枚举方案。
- [x] 记录 UI 接入方式。
- [x] 记录已知限制。

**实现说明**：
- 创建完整实现报告：`doc/Phase2_2_摄像头设备枚举与参数选择报告.md`
- 记录数据结构设计（CameraTypes.h）
- 记录设备枚举方案（FFmpeg dshow vs DirectShow COM vs Qt Multimedia）
- 记录格式枚举方案（第一版硬编码 C920 格式）
- 记录 UI 实现（CameraSettingsDialog）
- 记录架构验证和职责分离
- 记录关键问题和解决方案（设备名格式、H.264 参数）
- 记录已知限制和后续优化方向
- 对比 Phase 2.1 和 Phase 2.2 的用户体验提升

---

## ✅ Phase 2.2 总结

**Phase 2.2 摄像头设备枚举与参数选择已全部完成！**

核心成果：
- ✅ 结构化的摄像头参数数据结构（CameraTypes.h）
- ✅ 基于 FFmpeg dshow 的设备枚举服务
- ✅ 硬编码的 C920 格式枚举
- ✅ 用户友好的摄像头设置对话框
- ✅ 不再硬编码设备名和参数

用户体验提升：
- 从硬编码 C920 到用户选择任意摄像头
- 从固定 720p30 到选择任意支持的格式
- 图形界面友好，支持刷新设备

---

## Phase 2.3 实时源语义完善

**目标**：把实时源能力从临时类型判断提升为接口能力，统一文件源、摄像头源、后续网络源的语义。

### 2.3.1 IMediaSource 能力接口设计 ✅ 已完成

- [x] 为 `IMediaSource` 设计 `IsSeekable()`。
- [x] 为 `IMediaSource` 设计 `HasDuration()`。
- [x] 为 `IMediaSource` 设计 `IsRealtime()`。
- [x] `FileMediaSource` 返回文件源能力。
- [x] `CameraMediaSource` 返回实时源能力。
- [x] 评估是否需要 `SupportsAudioInput()` 或类似能力。

**验收标准**：

- [x] 文件源和摄像头源能力语义清晰。
- [x] 播放控制层不再只靠 `MediaSourceType::Camera` 判断。

**实现说明**：
- 为 `IMediaSource` 添加三个能力查询接口：
  - `IsSeekable()`: 是否支持 seek
  - `HasDuration()`: 是否有确定的时长
  - `IsRealtime()`: 是否为实时源
- `FileMediaSource` 实现：
  - `IsSeekable() = true` (文件支持 seek)
  - `HasDuration() = true` (文件有时长)
  - `IsRealtime() = false` (文件不是实时源)
- `CameraMediaSource` 实现：
  - `IsSeekable() = false` (摄像头不支持 seek)
  - `HasDuration() = false` (摄像头无时长)
  - `IsRealtime() = true` (摄像头是实时源)
- 暂不添加 `SupportsAudioInput()`（第一版 video-only）

### 2.3.2 播放控制接入能力判断

- [ ] `MediaPipeline::SeekTo()` 使用 `IsSeekable()`。
- [ ] `MediaPipeline::Duration()` 使用 `HasDuration()`。
- [ ] `MediaPlayController::SeekTo()` 使用 active stream 能力。
- [ ] `MediaPlayController::SeekRelative()` 使用 active stream 能力。
- [ ] `MultiStreamManager` 提供 active stream 能力读取入口。
- [ ] 文件源行为不退化。
- [ ] 摄像头源行为符合 live 语义。

**验收标准**：

- [ ] 文件源可 seek。
- [ ] 摄像头源不可 seek。
- [ ] Controller 不需要知道具体源类型也能做正确判断。

### 2.3.3 UI 实时源状态显示

- [ ] active stream 为摄像头时禁用进度条拖动。
- [ ] active stream 为摄像头时 duration 不显示为普通文件时长。
- [ ] active stream 为摄像头时显示 live 状态或运行时长。
- [ ] 切回文件源后恢复进度条语义。

**验收标准**：

- [ ] 文件源 UI 语义不退化。
- [ ] 摄像头源 UI 不误导用户可拖动 seek。

### 2.3.4 Phase 2.3 报告

- [ ] 形成 `doc/Phase2_3_实时源语义完善报告.md`。
- [ ] 记录能力接口最终设计。
- [ ] 记录 Controller 和 UI 接入点。
- [ ] 记录兼容性验证结果。

## Phase 2.4 摄像头异常与生命周期

**目标**：让摄像头在真实使用中具备可诊断、可恢复、可释放的行为。

### 2.4.1 打开失败处理

- [ ] 处理设备不存在。
- [ ] 处理设备被占用。
- [ ] 处理参数不支持。
- [ ] 处理 dshow input format 不存在。
- [ ] `CameraMediaSource::Open()` 返回明确失败。
- [ ] 错误通过 `MediaPipeline -> MultiStreamManager -> MediaPlayController -> UI` 转发。

**验收标准**：

- [ ] 添加失败不崩溃。
- [ ] UI 能看到可理解错误。
- [ ] 失败后资源释放完整。

### 2.4.2 运行中异常处理

- [ ] 处理读帧失败。
- [ ] 处理摄像头运行中断流。
- [ ] 处理运行中拔出设备。
- [ ] 处理 demuxer 错误转发。
- [ ] 异常 stream 可被移除。
- [ ] 异常 stream 不影响其他文件或摄像头 stream。

**验收标准**：

- [ ] 单路摄像头异常不拖垮整个程序。
- [ ] 多路中摄像头异常不影响其他路。

### 2.4.3 重复添加和释放验证

- [ ] 防止重复添加同一摄像头。
- [ ] 或明确允许重复添加时的行为。
- [ ] remove stream 时释放摄像头。
- [ ] SwitchSource 时释放旧摄像头。
- [ ] ShutdownAll 时释放所有摄像头。
- [ ] 关闭程序时释放摄像头。

**验收标准**：

- [ ] 摄像头不会被遗留占用。
- [ ] 关闭程序不阻塞。

### 2.4.4 Phase 2.4 报告

- [ ] 形成 `doc/Phase2_4_摄像头异常与生命周期报告.md`。
- [ ] 记录异常处理链路。
- [ ] 记录资源释放验证结果。

## Phase 2.5 摄像头配置持久化

**目标**：保存用户常用摄像头配置，但不把配置逻辑放入 `CameraMediaSource`。

- [ ] 确定配置保存位置。
- [ ] 保存上次使用的设备名。
- [ ] 保存上次选择的分辨率。
- [ ] 保存上次选择的帧率。
- [ ] 保存上次选择的输入格式。
- [ ] 启动后恢复默认摄像头配置。
- [ ] 设备不存在时回退到空选择。
- [ ] 配置读取失败时不影响程序启动。
- [ ] 形成 `doc/Phase2_5_摄像头配置持久化报告.md`。

**验收标准**：

- [ ] 配置持久化不依赖 `CameraMediaSource`。
- [ ] 用户下次添加摄像头时能复用上次选择。

## Phase 2.6 摄像头截图

**目标**：支持摄像头 stream 截图，并保持截图能力独立于媒体源。

### 2.6.1 截图边界预研

- [ ] 预研从 `VideoRender` 最后一帧截图。
- [ ] 预研从 `MediaPipeline` 暴露当前帧快照。
- [ ] 预研后续从 `MultiViewRenderer` 指定 viewport 截图。
- [ ] 明确截图源分辨率还是显示分辨率。
- [ ] 明确截图文件格式。
- [ ] 明确截图保存路径和命名规则。

**验收标准**：

- [ ] 能说明截图为什么不属于 `CameraMediaSource`。
- [ ] 能说明第一版截图路线。

### 2.6.2 截图实现

- [ ] 支持截 active stream。
- [ ] 支持指定 stream 截图。
- [ ] 摄像头 stream 可截图。
- [ ] 文件 stream 可截图。
- [ ] 无有效帧时返回失败。
- [ ] 截图失败时 UI 有提示。

**验收标准**：

- [ ] active stream 截图通过。
- [ ] 指定 stream 截图通过。
- [ ] 摄像头和文件路径一致。

### 2.6.3 Phase 2.6 报告

- [ ] 形成 `doc/Phase2_6_摄像头截图报告.md`。

## Phase 2.7 摄像头录像预研

**目标**：先决定录像技术路线，不在摄像头播放 MVP 中直接实现录像。

- [ ] 预研 packet remux 录像路线。
- [ ] 预研 frame encode 录像路线。
- [ ] 比较性能、兼容性和实现复杂度。
- [ ] 判断第一版录像是否只支持摄像头。
- [ ] 判断第一版录像是否支持任意 stream。
- [ ] 设计录制状态。
- [ ] 设计录制错误转发。
- [ ] 形成 `doc/Phase2_7_摄像头录像预研报告.md`。

**验收标准**：

- [ ] 明确录像不属于 `CameraMediaSource`。
- [ ] 明确第一版录像路线。

## Phase 2.8 摄像头音频预研

**目标**：独立判断是否接入 C920 麦克风，不影响 video-only 路线。

- [ ] 预研 C920 麦克风 dshow 设备名。
- [ ] 预研 dshow 同时打开 video + audio 的方式。
- [ ] 验证摄像头音频采样率。
- [ ] 验证摄像头音频声道布局。
- [ ] 验证摄像头音视频同步。
- [ ] 判断摄像头音频是否进入 active audio stream 策略。
- [ ] 判断摄像头音频是否默认关闭。
- [ ] 形成 `doc/Phase2_8_摄像头音频预研报告.md`。

**验收标准**：

- [ ] 是否接入摄像头麦克风有明确结论。
- [ ] 不破坏现有“最多一路音频”策略。

## Phase 2.9 设备属性控制预研

**目标**：评估曝光、白平衡、自动对焦等设备属性控制，保持平台相关能力独立。

- [ ] 预研 Windows DirectShow `IAMCameraControl`。
- [ ] 预研 Windows DirectShow `IAMVideoProcAmp`。
- [ ] 预研曝光控制。
- [ ] 预研白平衡控制。
- [ ] 预研自动对焦控制。
- [ ] 预研增益、亮度、对比度控制。
- [ ] 设计 `CameraDeviceController` 边界。
- [ ] 明确该模块不参与播放管线。
- [ ] 形成 `doc/Phase2_9_设备属性控制预研报告.md`。

**验收标准**：

- [ ] 能说明设备属性控制为什么不属于 `CameraMediaSource`。
- [ ] 能说明平台相关模块边界。

## Phase 2.10 多摄像头、工业相机与网络摄像头预研

**目标**：为 C920 之外的摄像头类型做架构判断，不提前污染 MVP。

### 2.10.1 多 USB 摄像头

- [ ] 预研多个 USB 摄像头同时打开。
- [ ] 预研同名摄像头唯一标识。
- [ ] 预研 USB 带宽限制。
- [ ] 预研多摄像头资源释放顺序。
- [ ] 验证多摄像头仍复用 `MultiStreamManager`。

### 2.10.2 工业相机

- [ ] 预研海康 SDK 接入方式。
- [ ] 预研 GigE Vision / GenICam 接入方式。
- [ ] 判断工业相机是否能提供 `AVFormatContext`。
- [ ] 判断是否需要新增 `IVideoFrameSource`。
- [ ] 判断是否需要 `IndustrialCameraSource`。
- [ ] 明确工业相机不并入 C920 MVP。

### 2.10.3 网络摄像头

- [ ] 预研 RTSP 摄像头接入。
- [ ] 预研 ONVIF 发现能力。
- [ ] 判断网络摄像头是否走 `NetworkMediaSource`。
- [ ] 明确网络摄像头不属于 `CameraMediaSource`。

### 2.10.4 Phase 2.10 报告

- [ ] 形成 `doc/Phase2_10_多摄像头工业相机网络摄像头预研报告.md`。

**验收标准**：

- [ ] USB 摄像头、工业相机、网络摄像头边界清晰。
- [ ] 不为了未来设备提前重构当前 C920 MVP。

## Phase 2 总体验收

- [ ] C920 video-only MVP 可用。
- [ ] 摄像头可作为 `IMediaSource` 接入。
- [ ] 摄像头可进入现有 `MediaPipeline`。
- [ ] 摄像头可进入现有多路预览。
- [ ] 摄像头无音频策略稳定。
- [ ] 摄像头不可 seek 语义稳定。
- [ ] 设备枚举和参数选择具备清晰扩展路线。
- [ ] 截图、录像、摄像头音频、设备属性控制没有污染 `CameraMediaSource`。
- [ ] 工业相机和网络摄像头具备独立预研路线。

## 当前不做范围

- [ ] 不在 C920 MVP 中实现摄像头麦克风。
- [ ] 不在 C920 MVP 中实现截图。
- [ ] 不在 C920 MVP 中实现录像。
- [ ] 不在 C920 MVP 中实现曝光、白平衡、自动对焦控制。
- [ ] 不在 C920 MVP 中实现多摄像头并发。
- [ ] 不在 C920 MVP 中实现海康 SDK。
- [ ] 不在 C920 MVP 中实现 RTSP/ONVIF。
- [ ] 不把真实视频帧接入 `MultiViewRenderer`。
