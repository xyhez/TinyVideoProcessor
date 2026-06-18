# Phase 2 摄像头完整功能预研报告

## 1. 预研目标

前一份 `Phase2_摄像头输入预研报告.md` 只定义了 C920 video-only MVP：打开一路 USB 摄像头，作为 `IMediaSource` 进入现有 `MediaPipeline`，并和文件视频一起进入多路预览。

这份报告预研“摄像头相关完整功能”应该如何规划，重点回答：

- 摄像头功能完整闭环包含哪些模块。
- 哪些功能属于 `CameraMediaSource`，哪些不属于。
- MVP 做完后还需要继续拆哪些阶段。
- 如何保持 `doc/架构指导.md` 中的分层边界。
- 如何为后续工业相机、网络摄像头、多摄像头预览留下扩展空间。

结论先行：

`CameraMediaSource` 只应该负责“按指定参数打开一个摄像头媒体源”。完整摄像头功能不应全部塞进 `CameraMediaSource`，而应拆成设备发现、参数选择、实时源能力、播放管线、UI 配置、异常处理、截图录像、设备控制等多个边界。

## 2. 摄像头完整功能范围

完整摄像头功能可以分为 9 类：

| 模块 | 功能 | 是否属于 MVP | 归属建议 |
| --- | --- | --- | --- |
| 摄像头打开 | 按设备名打开 C920 video-only | 是 | `CameraMediaSource` |
| 设备枚举 | 枚举系统摄像头列表 | 否 | 设备枚举服务 |
| 能力枚举 | 枚举分辨率、帧率、输入格式 | 否 | 设备枚举服务 |
| 参数配置 | 选择设备、分辨率、帧率、格式 | 否 | UI + 配置模型 |
| 实时预览 | 摄像头进入单路/多路画面 | 是 | 现有播放管线 |
| 实时源控制 | 禁止 seek、无 duration、运行时长显示 | 是 | `IMediaSource` 能力 + Controller/UI |
| 异常处理 | 设备占用、拔出、打开失败、读帧失败 | 部分 | Source + Pipeline + Controller |
| 设备属性控制 | 曝光、白平衡、自动对焦、增益 | 否 | 平台相关设备控制模块 |
| 截图录像 | 截取当前帧、录制摄像头流 | 否 | 截图/录制模块，不属于 Source |

## 3. 架构边界

完整摄像头能力建议保持下面的边界：

```text
MainWindow
    -> CameraSelectDialog / CameraSettingsPanel
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline
                -> CameraMediaSource
                -> demuxer / decoder / VideoRender / AudioPlayer

CameraDeviceEnumerator
    -> 查询系统摄像头和能力
    -> 不打开播放管线

CameraDeviceController
    -> 控制曝光、白平衡、对焦等设备属性
    -> 不参与解码和渲染

Recorder / SnapshotService
    -> 录制或截图
    -> 不放进 CameraMediaSource
```

关键原则：

- `MainWindow` 不直接管理摄像头 FFmpeg 上下文。
- `MediaPlayController` 继续作为 UI 对播放能力的入口。
- `MultiStreamManager` 只管理 stream，不关心 stream 来自文件还是摄像头。
- `MediaPipeline` 保持单路管线职责，不引入摄像头特例。
- `CameraMediaSource` 不做设备枚举 UI、不做截图、不做录像、不做设备属性控制。
- 平台相关逻辑集中在基础设施层，避免污染领域层和 UI 层。

## 4. 数据结构建议

为了避免后续到处传散乱的字符串和整数，建议逐步引入摄像头配置结构。

```cpp
struct CameraDeviceInfo {
    QString displayName;
    QString deviceName;
    QString backend;
};

struct CameraFormatOption {
    int width = 1280;
    int height = 720;
    int frameRate = 30;
    QString pixelFormat;
    QString codecName;
};

struct CameraOpenOptions {
    CameraDeviceInfo device;
    CameraFormatOption format;
    bool enableAudio = false;
};
```

第一版 MVP 可以只用设备名、宽、高、帧率，不必一次性引入完整结构。但后续做 UI 参数选择时，应尽快转成结构化配置。

## 5. 设备枚举与能力枚举

### 5.1 设备枚举目标

完整功能需要用户可以从 UI 里选择摄像头，而不是手写设备名。

需要枚举：

- 视频输入设备列表。
- 设备显示名。
- 后端类型，例如 `dshow`。
- 可选：设备唯一标识，避免同名设备冲突。

Windows 第一阶段可以基于 FFmpeg dshow 能力：

```text
ffmpeg -list_devices true -f dshow -i dummy
```

长期方案可以考虑：

- FFmpeg/libavdevice 枚举 API。
- Windows DirectShow COM 枚举。
- Qt Multimedia 枚举。

建议路线：

1. MVP：手动配置设备名。
2. 第二阶段：使用 FFmpeg 或外部命令输出做设备列表验证。
3. 稳定阶段：封装 `CameraDeviceEnumerator`，向 UI 返回结构化设备列表。

### 5.2 能力枚举目标

能力枚举用于获取某个设备支持的：

- 分辨率。
- 帧率。
- 输入格式，例如 MJPEG、YUY2、H.264。
- 可能的最小/最大参数范围。

Windows dshow 典型命令：

```text
ffmpeg -list_options true -f dshow -i video="实际设备名"
```

能力枚举结果不应直接进入 `CameraMediaSource`。推荐由 `CameraDeviceEnumerator` 或后续 `CameraCapabilityService` 提供，UI 选择后再构造 `CameraOpenOptions`。

## 6. CameraMediaSource 完整职责

`CameraMediaSource` 的完整职责应限定为：

- 接收 `CameraOpenOptions` 或等价参数。
- 根据平台后端构造 FFmpeg input URL。
- 设置 `AVDictionary` 打开参数。
- 调用 `avformat_open_input()`。
- 调用 `avformat_find_stream_info()`。
- 提供 `AVFormatContext*`。
- 填充基础 `Fileinfo`。
- 在 `Close()` 中释放 FFmpeg 资源。
- 暴露 `GetType() == MediaSourceType::Camera`。

示例参数：

```cpp
AVDictionary* options = nullptr;
av_dict_set(&options, "video_size", "1280x720", 0);
av_dict_set(&options, "framerate", "30", 0);

AVInputFormat* inputFormat = av_find_input_format("dshow");
avformat_open_input(&formatContext,
                    "video=Logitech HD Pro Webcam C920",
                    inputFormat,
                    &options);
```

`CameraMediaSource` 不应承担：

- 设备列表枚举。
- UI 参数选择。
- 多路布局。
- 主动启动线程。
- 解码。
- 渲染。
- 截图。
- 录像。
- 摄像头属性控制。
- 自动重连策略。

## 7. 实时源能力接口

摄像头完整功能要求系统明确“实时源”和“文件源”的语义差异。

推荐后续给 `IMediaSource` 增加能力查询：

```cpp
virtual bool IsSeekable() const = 0;
virtual bool HasDuration() const = 0;
virtual bool IsRealtime() const = 0;
```

文件源：

```text
IsSeekable = true
HasDuration = true
IsRealtime = false
```

摄像头源：

```text
IsSeekable = false
HasDuration = false
IsRealtime = true
```

影响范围：

- `MediaPipeline::SeekTo()` 应拒绝不可 seek 源。
- `MediaPlayController::SeekTo()` 应拒绝不可 seek 的 active stream。
- UI 进度条对实时源应禁用拖动或隐藏。
- duration 显示应改为实时运行时长或 `Live` 状态。
- 播放完成信号不应依赖摄像头 EOF。

这一步比只判断 `MediaSourceType::Camera` 更干净，但影响接口较多，建议放在 C920 MVP 跑通后再做收敛。

## 8. UI 完整功能

完整 UI 不应只是一个“添加摄像头”按钮，而应逐步演进为：

### 8.1 添加摄像头入口

- 从工具栏或菜单进入。
- 打开摄像头选择对话框。
- 选择设备后作为一路 stream 加入多路预览。

### 8.2 摄像头选择对话框

字段建议：

- 设备列表。
- 分辨率下拉框。
- 帧率下拉框。
- 输入格式下拉框。
- 是否启用摄像头音频，第一版禁用或隐藏。
- 预览/确认按钮。

### 8.3 多路预览联动

- 摄像头 stream 和文件 stream 共享 active stream 选择逻辑。
- 摄像头 stream 不显示可 seek 的 duration。
- 摄像头 stream 被选中后，进度条应呈现 live 状态。
- 摄像头无音频时，不应破坏文件音频播放。

### 8.4 设置持久化

可以保存：

- 上次使用的摄像头设备名。
- 上次选择的分辨率。
- 上次选择的帧率。
- 上次选择的输入格式。

配置持久化不属于 `CameraMediaSource`，应由应用层或配置服务完成。

## 9. 异常处理

摄像头异常比文件源更多，完整功能必须覆盖：

| 场景 | 预期行为 |
| --- | --- |
| 设备不存在 | 添加失败，UI 显示错误 |
| 设备被占用 | 添加失败，提示设备被其他程序占用 |
| 参数不支持 | 打开失败，提示切换分辨率或帧率 |
| 运行中拔出 | stream 进入错误状态，可移除 |
| 读帧失败 | 记录错误，必要时停止该 stream |
| 关闭程序 | 正常释放设备句柄 |
| 重复添加同一设备 | 第一版建议拒绝或提示 |

实现边界：

- `CameraMediaSource::Open()` 返回打开失败。
- `demuxer` 读帧失败应向 `MediaPipeline` 报错。
- `MediaPipeline` 将错误转发给 `MultiStreamManager`。
- `MediaPlayController` 再转发给 UI。
- UI 不直接碰 FFmpeg 错误码，只展示业务错误。

## 10. 摄像头音频

C920 带麦克风，但完整接入不建议和视频 MVP 混在一起。

原因：

- 摄像头音频是输入设备，不是文件里的音频轨。
- dshow 需要同时指定 video 和 audio 输入。
- 摄像头麦克风有采集延迟，和视频同步需要单独验证。
- 多路播放策略当前是“最多一路 active audio stream”，摄像头音频会影响这个策略。
- 摄像头麦克风可能和系统默认麦克风设备名不同。

建议路线：

1. Phase 2.1：摄像头 video-only。
2. Phase 2.4 或后续：预研 `CameraAudioSource` 或 camera audio track。
3. 决定摄像头麦克风是否作为 active audio stream 候选。
4. 验证音视频同步和采样格式。

在用户已明确“无音频流直接静音”的策略下，当前阶段保持 video-only 是正确的。

## 11. 截图与录像

### 11.1 截图

截图不应由 `CameraMediaSource` 实现。

合理位置：

- 从渲染器最后一帧截图。
- 或从 `MediaPipeline` 暴露当前视频帧快照。
- 或后续在统一 `MultiViewRenderer` 中按 stream 截取当前 viewport。

截图需要考虑：

- 当前帧格式。
- 色彩转换。
- 截图分辨率是源分辨率还是显示分辨率。
- 多路时截 active stream 还是指定 stream。

### 11.2 录像

录像也不应由 `CameraMediaSource` 实现。

合理位置：

- 新增 `Recorder` 或 `RecordingPipeline`。
- 从 packet 层直接 remux。
- 或从 frame 层重新编码。

两种路线：

| 路线 | 优点 | 风险 |
| --- | --- | --- |
| packet remux | 性能高 | 要求输入编码和容器兼容 |
| frame encode | 兼容性好 | CPU/GPU 编码复杂度高 |

第一版摄像头播放不应包含录像。

## 12. 设备属性控制

曝光、白平衡、自动对焦、增益、亮度、对比度这类属性不属于 FFmpeg 通用播放链路。

Windows 下通常需要 DirectShow 相关能力，例如：

- `IAMCameraControl`
- `IAMVideoProcAmp`
- 设备属性页

这些能力应封装在独立模块，例如：

```text
CameraDeviceController
    -> SetExposure()
    -> SetWhiteBalance()
    -> SetAutoFocus()
    -> SetGain()
```

它不应进入 `MediaPipeline`，也不应进入 `CameraMediaSource` 的核心打开逻辑。

## 13. 多摄像头

多摄像头是完整能力的一部分，但不是第一版目标。

需要考虑：

- 同一设备不能重复打开。
- USB 带宽限制。
- 多个 C920 同名设备的唯一标识。
- 多路摄像头帧率下降。
- 多路摄像头同时关闭的释放顺序。
- 多路 active stream 和 active audio stream 仍沿用现有策略。

架构上，多摄像头应自然复用：

```text
CameraMediaSource #1 -> MediaPipeline #1
CameraMediaSource #2 -> MediaPipeline #2
CameraMediaSource #3 -> MediaPipeline #3
```

不要在一个 `CameraMediaSource` 里管理多个物理摄像头。

## 14. 工业相机与网络摄像头

### 14.1 工业相机

海康网口工业相机通常不是普通 USB UVC 摄像头，常见接入方式是厂商 SDK、GigE Vision 或 GenICam。

这类设备不一定天然提供 `AVFormatContext`，因此未必适合直接实现当前 `IMediaSource`。

长期可能需要新增更通用的帧源抽象：

```text
IMediaSource
    -> 面向 FFmpeg AVFormatContext 的源

IVideoFrameSource
    -> 面向已经采集到的原始帧
```

但当前项目还在 FFmpeg 管线阶段，不建议现在提前改成通用帧源。先把 USB C920 跑通，再根据真实工业相机需求决定是否抽象。

### 14.2 网络摄像头

RTSP/ONVIF 摄像头更适合走 `NetworkMediaSource`，不是 `CameraMediaSource`。

后续路线：

- `CameraMediaSource`：本机采集设备，USB/UVC/dshow/v4l2。
- `NetworkMediaSource`：RTSP/HTTP/RTMP 等网络流。
- `IndustrialCameraSource` 或 `FrameSourceAdapter`：厂商 SDK 或 GigE Vision。

## 15. 推荐阶段拆分

### Phase 2.1 C920 video-only MVP

目标：打通 C920 视频输入到现有多路预览。

内容：

- 手动设备名。
- dshow video-only。
- 无音频。
- 禁止 seek。
- 单路和文件 + 摄像头双路验证。

### Phase 2.2 摄像头设备枚举与参数选择

目标：让用户不再手写设备名。

内容：

- 枚举视频设备。
- 枚举设备能力。
- 添加摄像头选择对话框。
- 用户可选择分辨率、帧率和输入格式。

### Phase 2.3 实时源语义完善

目标：把实时源能力从类型判断提升为接口能力。

内容：

- `IMediaSource` 增加能力查询。
- Controller 和 UI 根据能力禁用 seek。
- duration/position 显示区分文件和 live。

### Phase 2.4 摄像头异常与生命周期

目标：让摄像头在真实使用中可恢复、可诊断。

内容：

- 设备占用提示。
- 设备拔出提示。
- 读帧错误转发。
- 重复添加同一设备防护。
- 摄像头释放验证。

### Phase 2.5 摄像头配置持久化

目标：保存用户上次摄像头选择。

内容：

- 保存设备名。
- 保存分辨率、帧率、输入格式。
- 启动后恢复默认选择。

### Phase 2.6 摄像头截图

目标：支持指定摄像头 stream 截图。

内容：

- 截 active stream。
- 截指定 stream。
- 明确截图分辨率和格式。

### Phase 2.7 摄像头录像预研

目标：决定录像技术路线。

内容：

- packet remux 预研。
- frame encode 预研。
- 录制文件命名和状态管理。

### Phase 2.8 摄像头音频预研

目标：决定是否接 C920 麦克风。

内容：

- dshow audio 输入验证。
- 音视频同步验证。
- active audio 策略调整。

### Phase 2.9 工业相机/网络摄像头预研

目标：为后续非 USB 摄像头扩展做架构判断。

内容：

- 海康 SDK / GigE Vision 接入方式。
- 是否需要 `IVideoFrameSource`。
- RTSP 是否作为 `NetworkMediaSource`。

## 16. 结论

摄像头完整功能不应该以“一个大 `CameraMediaSource`”完成，而应该按能力边界分阶段推进。

建议当前路线：

1. 先完成 C920 video-only MVP。
2. 再做设备枚举和参数选择。
3. 再完善实时源语义和异常处理。
4. 截图、录像、摄像头音频、工业相机分别单独预研和实施。

这样既符合 `doc/架构指导.md` 的分层路线，也能保证每一步都有可运行、可验证的版本。
