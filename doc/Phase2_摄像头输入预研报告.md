# Phase 2 摄像头输入预研报告

## 1. 预研目标

当前项目已经具备：

- `IMediaSource` 媒体源抽象。
- `FileMediaSource` 文件媒体源实现。
- `MediaPipeline` 单路播放管线。
- `MultiStreamManager` 多路管线管理。
- `MediaPlayController` 作为 UI 层对外入口。
- 多路文件源预览和 active stream / active audio stream 策略。

Phase 2 的目标不是为摄像头另起一套播放链路，而是新增 `CameraMediaSource`，让摄像头和文件一样作为 `IMediaSource` 进入现有管线。

目标结构：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline #1 -> FileMediaSource
            -> MediaPipeline #2 -> CameraMediaSource
```

这样文件视频和 Logitech C920 摄像头可以混合进入多路预览，后续也能继续接入网络流或工业相机源。

## 2. C920 接入方式判断

Logitech C920 是 USB UVC 摄像头。在 Windows + FFmpeg 环境下，第一版建议通过 DirectShow，也就是 FFmpeg 的 `dshow` input format 接入。

设备枚举命令：

```text
ffmpeg -list_devices true -f dshow -i dummy
```

能力枚举命令：

```text
ffmpeg -list_options true -f dshow -i video="Logitech HD Pro Webcam C920"
```

打开摄像头的核心形式：

```cpp
AVInputFormat* inputFormat = av_find_input_format("dshow");
avformat_open_input(&formatContext,
                    "video=Logitech HD Pro Webcam C920",
                    inputFormat,
                    &options);
```

设备名称需要以实际枚举结果为准。C920 在不同驱动、系统语言、Qt Creator 运行环境下，名称可能不是完全固定字符串，所以实现前必须先支持“可配置设备名”，不要把设备名硬编码死。

## 3. CameraMediaSource 边界

`CameraMediaSource` 应该是领域层媒体源实现，职责只包括：

- 保存摄像头设备名。
- 保存采集参数，比如宽度、高度、帧率、输入格式偏好。
- 使用 FFmpeg 打开 dshow 设备。
- 调用 `avformat_find_stream_info()` 获取流信息。
- 持有并释放 `AVFormatContext`。
- 通过 `GetFormatContect()` 把上下文交给现有 `demuxer` 使用。
- 通过 `GetSourceInfo()` 暴露基础媒体信息。

`CameraMediaSource` 不应该做：

- 不管理 `MediaPipeline`。
- 不创建 `decoder`。
- 不创建 `VideoRender`。
- 不参与 active audio stream 策略。
- 不参与多路布局。
- 不处理 UI 设备选择。

第一版建议接口形态：

```cpp
class CameraMediaSource : public IMediaSource {
public:
    CameraMediaSource() = default;
    explicit CameraMediaSource(QString deviceName);

    bool Open() override;
    bool Close() override;
    AVFormatContext* GetFormatContect() override;
    MediaSourceType GetType() override;
    void SetInfo(QString sourceInfo) override;
    Fileinfo& GetSourceInfo() override;

    void SetResolution(int width, int height);
    void SetFrameRate(int frameRate);

private:
    AVFormatContext* formatContext_ = nullptr;
    Fileinfo sourceInfo_;
    QString deviceName_;
    int width_ = 1280;
    int height_ = 720;
    int frameRate_ = 30;
};
```

`SetInfo(QString sourceInfo)` 对摄像头源的语义建议定义为“设置设备名”。例如：

```cpp
cameraSource->SetInfo("Logitech HD Pro Webcam C920");
```

## 4. 实时源能力缺口

文件源和摄像头源的最大差异不是打开方式，而是播放语义：

| 能力 | 文件源 | 摄像头源 |
| --- | --- | --- |
| duration | 有 | 无 |
| seek | 支持 | 不支持 |
| EOF | 正常结束 | 通常不主动结束 |
| 音频 | 可选 | 第一版建议不接 |
| 时间轴 | 文件时间轴 | 实时采集时间轴 |

当前 `IMediaSource` 只有 `GetType()`，没有显式能力接口。现有代码里：

- `MediaPipeline::Duration()` 从 `GetSourceInfo().duration_` 读取。
- `MediaPipeline::SeekTo()` 默认允许 seek。
- `demuxer::SeekTo()` 直接调用 `av_seek_frame()`。
- UI 层进度条默认按 duration/position 工作。

因此摄像头接入前，需要在实现计划里明确实时源策略。

推荐策略：

1. 第一版可以先通过 `MediaSourceType::Camera` 判断实时源，避免大范围接口改动。
2. 摄像头源 `duration_ = 0`。
3. 摄像头源不允许 seek，`MediaPipeline::SeekTo()` 或 `MediaPlayController::SeekTo()` 应该直接拒绝。
4. UI 进度条对摄像头源应禁用拖动，或至少拖动不触发 seek。
5. 摄像头 position 可以继续显示实时运行时长，但不能被解释为可 seek 文件位置。

后续更稳的接口演进是给 `IMediaSource` 增加能力查询：

```cpp
virtual bool IsSeekable() const = 0;
virtual bool HasDuration() const = 0;
virtual bool IsRealtime() const = 0;
```

但这会影响 `FileMediaSource`、`CameraMediaSource` 和播放控制链路。第一版可以先不做完整能力接口，等摄像头单路预览跑通后再收敛。

## 5. 音频策略

C920 带麦克风，但第一版不建议接摄像头音频。

原因：

- 当前多路音频策略已经以“最多一路 active audio stream”为核心。
- 摄像头麦克风会引入采集延迟、系统输入设备权限、回声和采样格式问题。
- 当前目标是验证视频实时预览和多路混合显示。
- 项目已经支持无音频流视频，摄像头 video-only 能复用这条链路。

第一版摄像头源建议：

- 只打开 `video=设备名`。
- 不打开 `audio=设备名`。
- `sourceInfo.hasAudio_ = false`。
- `MultiStreamManager::StreamHasAudio()` 返回 false。
- 摄像头成为 active stream 时，active audio stream 可保持原文件音频，或者按当前策略切到无效音频流。由于用户期望“视频/摄像头一起渲染”，第一版应优先保持现有 active audio 策略，不为摄像头增加声音。

## 6. 视频格式风险

C920 常见输出能力包括 MJPEG、YUY2、H.264 或不同分辨率/帧率组合，实际能力必须以 `-list_options` 输出为准。

风险点：

- dshow 输出可能是压缩 MJPEG，也可能是 raw YUY2。
- 解码后的 `AVFrame::format` 不一定是当前 `VideoRender` 支持的 `AV_PIX_FMT_YUV420P`。
- 当前 `VideoRender` 只支持 `AV_PIX_FMT_YUV420P`，对其他格式只输出诊断日志并跳过绘制。
- 摄像头高分辨率或高帧率可能带来更高 CPU 压力。

推荐第一版参数：

```text
video_size=1280x720
framerate=30
```

如果 C920 在 1280x720 30fps 下输出的帧格式不被当前渲染器支持，则下一步再处理视频格式转换，不应在 `CameraMediaSource` 里直接做格式转换。

格式转换应属于解码输出到渲染输入之间的边界，不能塞进媒体源。

## 7. 与多路预览的关系

摄像头应该接入多路预览，而且应与文件视频走同一条入口：

```cpp
auto source = std::make_shared<CameraMediaSource>("Logitech HD Pro Webcam C920");
controller->AddStream(source, rendererContainer);
```

推荐验证顺序：

1. 单路 C920 预览：只打开摄像头，验证 `CameraMediaSource -> MediaPipeline -> VideoRender`。
2. 文件 + C920 双路预览：一路文件、一路摄像头，验证 `MultiStreamManager` 生命周期。
3. 文件有音频 + C920 无音频：验证 active audio 策略不退化。
4. C920 active stream 切换：验证进度条、音频、选中态不会出现错误语义。

第一版不建议直接进入统一 `MultiViewRenderer` 真实帧接入。当前多个 `VideoRender + QWidget` 路径已经能承载多路验证，更适合作为摄像头输入的首个验证环境。

## 8. UI 接入建议

第一版 UI 可以很小：

- 新增“添加摄像头”入口。
- 先使用固定或配置的设备名。
- 默认参数为 1280x720、30fps。
- 添加成功后像普通 stream 一样进入当前多路布局。

不建议第一步就做完整摄像头设置面板。完整面板可以放到后续任务：

- 枚举设备列表。
- 枚举分辨率和帧率。
- 允许用户选择输入格式。
- 保存上次使用的摄像头参数。

## 9. 第一版不做范围

第一版摄像头接入不做：

- 不接摄像头麦克风。
- 不做截图。
- 不做录像。
- 不做设备属性控制，如曝光、白平衡、自动对焦。
- 不做多摄像头并发。
- 不做工业相机 SDK。
- 不做 RTSP/网络摄像头。
- 不做 HDR、色彩管理和复杂像素格式转换。
- 不替换当前多个 `VideoRender` 的多路预览路径。

## 10. 推荐实施顺序

### 2.1 设备能力确认

拿到 C920 后先运行：

```text
ffmpeg -list_devices true -f dshow -i dummy
ffmpeg -list_options true -f dshow -i video="实际设备名"
```

记录设备名、支持分辨率、帧率和输入格式。

### 2.2 `CameraMediaSource` 空壳

新增 `CameraMediaSource.h/.cpp`，实现 `IMediaSource` 接口，但先不接 UI。

验收点：

- 类加入构建。
- 不影响 `FileMediaSource`。
- `GetType()` 返回 `MediaSourceType::Camera`。

### 2.3 dshow 打开摄像头

在 `CameraMediaSource::Open()` 中使用 `av_find_input_format("dshow")` 和 `avformat_open_input()` 打开视频设备。

验收点：

- 打开成功。
- 能获取 `AVFormatContext`。
- `demuxer` 能识别视频流。

### 2.4 实时源 seek 防护

在播放控制链路中明确摄像头不可 seek。

建议先做最小防护：

- `MediaPipeline::SeekTo()` 检查 `mediaSource_->GetType() == MediaSourceType::Camera` 时返回 false。
- `MediaPlayController::SeekTo()` 对 active camera stream 不执行 seek。
- UI 层后续再禁用进度条拖动。

### 2.5 单路摄像头预览

通过 `MediaPlayController::Initialize()` 或 `AddStream()` 添加 C920。

验收点：

- 能显示实时画面。
- 关闭程序正常。
- 暂停/继续行为符合当前管线能力。
- 无音频策略不退化。

### 2.6 文件 + 摄像头混合预览

通过现有多路接口添加一路文件和一路摄像头。

验收点：

- 文件和摄像头可以同时显示。
- active stream 切换正常。
- 文件音频仍可播放。
- 摄像头无音频时不会把音频链路带坏。
- remove stream / shutdown 正常释放摄像头。

## 11. 结论

Phase 2 可以在当前架构上推进，但必须把摄像头定义为实时 `IMediaSource`，不能把它做成 UI 或播放管线的特例。

最小可行路线是：

```text
CameraMediaSource
    -> dshow 打开 C920 video-only
    -> 复用 demuxer / decoder / VideoRender
    -> 复用 MediaPipeline
    -> 复用 MultiStreamManager
    -> 接入现有多路预览
```

第一版重点验证“摄像头作为实时视频源进入现有多路管线”。设备枚举、摄像头音频、参数面板和工业相机 SDK 都应放在后续阶段。
