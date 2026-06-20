# Phase 2.1 C920 Video-Only MVP 实现报告

## 1. 概述

**实施时间**：2026-06-20

**目标**：通过 FFmpeg dshow 接入 Logitech C920 摄像头，作为 video-only 实时媒体源进入现有播放管线，验证摄像头与文件视频共用多路预览体系的可行性。

**结论**：✅ 已完成，所有验收标准通过。

---

## 2. 设备信息

### 2.1 摄像头型号
- **设备名称**：`HD Pro Webcam C920`
- **厂商**：Logitech
- **连接方式**：USB UVC

### 2.2 设备能力

通过 `ffmpeg -list_options true -f dshow -i video="HD Pro Webcam C920"` 获取：

**支持的输出格式**：

1. **YUYV422** (raw 格式)
   - 1280x720 @ 10fps (max)
   - 1920x1080 @ 5fps (max)

2. **H.264** (硬件编码) ✅ 推荐
   - 640x480 @ 30fps
   - 1280x720 @ 30fps
   - 1920x1080 @ 30fps

3. **MJPEG** (压缩格式)
   - 1280x720 @ 30fps
   - 1920x1080 @ 30fps

### 2.3 第一版选择参数

```cpp
设备名：    "HD Pro Webcam C920"
格式：      自动（FFmpeg 自动选择，实际输出 H.264）
分辨率：    1280x720
帧率：      30fps
rtbufsize： 100M
```

**选择理由**：
- H.264 由摄像头硬件编码，CPU 占用低
- 1280x720@30fps 平衡性能和画质
- rtbufsize 增大到 100M 避免实时流缓冲区溢出

---

## 3. 架构实现

### 3.1 CameraMediaSource 职责边界

`CameraMediaSource` 作为领域层媒体源实现，职责清晰：

**负责**：
- 保存摄像头设备名和采集参数（分辨率、帧率）
- 使用 FFmpeg dshow 打开摄像头设备
- 调用 `avformat_find_stream_info()` 获取流信息
- 持有并释放 `AVFormatContext`
- 通过 `GetFormatContect()` 交给现有 demuxer 使用
- 通过 `GetSourceInfo()` 暴露媒体信息

**不负责**：
- 不管理 `MediaPipeline`
- 不创建 decoder、renderer、audio player
- 不参与 active stream / active audio stream 策略
- 不参与多路布局
- 不处理 UI 设备选择

### 3.2 实现文件

**头文件**：`src/domain/CameraMediaSource.h`

```cpp
class CameraMediaSource : public IMediaSource {
public:
    CameraMediaSource() = default;
    explicit CameraMediaSource(QString deviceName);
    ~CameraMediaSource();

    bool Open() override;
    bool Close() override;
    AVFormatContext* GetFormatContect() override;
    MediaSourceType GetType() override;
    Fileinfo& GetSourceInfo() override;
    void SetInfo(QString deviceName) override;

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

**实现文件**：`src/domain/CameraMediaSource.cpp`

**关键实现**：
- 注册 `avdevice_register_all()`
- 使用 `av_find_input_format("dshow")`
- 构造设备 URL：`video=设备名`
- 设置参数：`video_size`、`framerate`、`rtbufsize`
- 填充 `sourceInfo_`（hasVideo=true, hasAudio=false, duration=0）

### 3.3 实时源特性

| 能力 | 文件源 | 摄像头源 |
| --- | --- | --- |
| duration | 有 | 无（duration=0） |
| seek | 支持 | 不支持 |
| EOF | 正常结束 | 不主动结束 |
| 音频 | 可选 | 第一版不接 |
| 时间轴 | 文件时间轴 | 实时采集时间轴 |

### 3.4 实时源 seek 防护

**实施位置**：`src/domain/MediaPipeline.cpp`

```cpp
bool MediaPipeline::SeekTo(double seconds)
{
    // 实时源（摄像头）不支持 seek
    if (mediaSource_ && mediaSource_->GetType() == MediaSourceType::Camera) {
        qWarning() << "MediaPipeline seek rejected: camera source is not seekable";
        return false;
    }
    // ...
}
```

**防护策略**：
- 通过 `MediaSourceType::Camera` 判断实时源
- `SeekTo()` 和 `SeekRelative()` 直接返回 false
- 输出警告日志但不崩溃

---

## 4. 关键问题与解决方案

### 4.1 问题一：rtbufsize 缓冲区溢出

**现象**：
```
[dshow @ ...] real-time buffer [HD Pro Webcam C920] [video input] too full or near too full (100% of size: 3041280 [rtbufsize parameter])! frame dropped!
```

**原因**：
- FFmpeg 从摄像头读取帧的速度不够快
- 默认 rtbufsize 约 3MB，缓冲区满后帧被丢弃
- 没有画面显示

**解决方案**：
```cpp
av_dict_set(&options, "rtbufsize", "100M", 0);
```

**效果**：缓冲区溢出警告消失，画面正常显示。

---

### 4.2 问题二：yuvj420p 格式不兼容

**现象**：
```
VideoRender unsupported frame format: pix_fmt=yuvj420p size=1280x720 ...
```

**原因**：
- C920 输出 `yuvj420p`（JPEG 色彩范围，pc range 0-255）
- 当前 `VideoRender` 只支持 `yuv420p`（tv range 16-235）
- 格式不匹配导致无画面

**解决方案**：在 `decoder.cpp` 添加视频格式归一化逻辑

```cpp
// 如果是视频且格式不是 yuv420p，进行格式转换
if (decoder_type == "视频" && frame->format != AV_PIX_FMT_YUV420P) {
    // 使用 sws_scale 转换成 yuv420p
    m_swsCtx = sws_getCachedContext(...);
    sws_scale(m_swsCtx, frame->data, frame->linesize, 
              0, frame->height, 
              outFrame->data, outFrame->linesize);
}
```

**优点**：
- 统一视频输出格式为 `yuv420p`
- 支持任意输入格式（yuvj420p, nv12, rgb 等）
- 复用现有的 `m_swsCtx`（已用于硬解转换）
- 符合 Phase 3.5 格式归一化设计

**效果**：摄像头画面正常显示，格式转换日志输出：
```
视频格式转换: yuvj420p -> yuv420p
VideoRender accepted frame format: pix_fmt=yuv420p
```

---

## 5. 测试验证

### 5.1 单路摄像头预览（Phase 2.1.6）

**测试步骤**：
1. 启动程序
2. 点击 "Add Camera" 按钮
3. 观察摄像头画面
4. 测试启动/暂停/停止
5. 移除摄像头流
6. 关闭程序

**验收结果**：✅ 全部通过
- 摄像头画面正常显示
- 启动/暂停/停止不阻塞
- 移除流后资源释放正常
- 关闭程序不阻塞

### 5.2 文件 + 摄像头混合多路预览（Phase 2.1.7）

**测试步骤**：
1. 添加一路文件视频
2. 添加一路 C920 摄像头
3. 验证两路同时显示
4. 点击画面切换 active stream
5. 验证文件音频播放
6. 单独移除文件流，验证摄像头继续运行
7. 单独移除摄像头流，验证文件继续运行

**验收结果**：✅ 全部通过
- 文件和摄像头可以同时显示
- Active stream 切换正常
- 文件音频正常播放
- 摄像头无音频不影响音频策略
- 单独移除任一路后另一路运行正常
- 布局切换正常（Single/Grid2x2）

---

## 6. UI 实现

### 6.1 测试入口

**位置**：`src/ui/mainwindow.ui` + `src/ui/mainwindow.cpp`

**实现**：
- 新增 `pushButton_addCamera` 按钮（按钮文本：Add Camera）
- 实现 `MainWindow::AddCameraStream()` 函数
- 连接信号槽：`pushButton_addCamera::clicked -> AddCameraStream()`

**代码示例**：
```cpp
void MainWindow::AddCameraStream() {
    auto cameraSource = std::make_shared<CameraMediaSource>();
    cameraSource->SetInfo("HD Pro Webcam C920");
    cameraSource->SetResolution(1280, 720);
    cameraSource->SetFrameRate(30);

    const MediaPlayController::StreamId streamId =
        mediaPlayController_->AddStream(cameraSource, container);

    streamDisplayNames_.insert(streamId, "C920 Camera");
    RefreshStreamView();
}
```

---

## 7. 架构验证

### 7.1 架构目标

```
MainWindow
  -> MediaPlayController
    -> MultiStreamManager
      -> MediaPipeline #1 -> FileMediaSource
      -> MediaPipeline #2 -> CameraMediaSource
```

### 7.2 验证结果

✅ **摄像头成功作为 `IMediaSource` 接入现有架构**：
- 摄像头和文件视频走同一套播放管线
- 复用 demuxer / decoder / VideoRender
- 复用 MediaPipeline 单路播放管线
- 复用 MultiStreamManager 多路管理
- 复用 MediaPlayController 播放控制

✅ **架构约束得到遵守**：
- `CameraMediaSource` 不创建 decoder、renderer
- `CameraMediaSource` 不参与 active stream 策略
- `MediaPipeline` 保持单路职责
- `MainWindow` 不直接访问 FFmpeg 上下文

---

## 8. 已知限制

### 8.1 第一版不包含功能

- ❌ 不接摄像头麦克风（C920 带麦克风但不接入）
- ❌ 不做设备枚举（设备名写死或配置）
- ❌ 不做参数选择 UI（分辨率/帧率固定）
- ❌ 不做截图功能
- ❌ 不做录像功能
- ❌ 不做设备属性控制（曝光、白平衡、对焦）
- ❌ 不支持多摄像头并发
- ❌ 不支持工业相机 SDK
- ❌ 不支持 RTSP/ONVIF 网络摄像头

### 8.2 技术限制

1. **设备名称不可移植**：
   - 不同系统/驱动下设备名可能不同
   - 需要先运行 `ffmpeg -list_devices` 确认实际设备名

2. **格式依赖 C920 能力**：
   - 当前依赖 C920 输出 H.264 或 MJPEG
   - 其他摄像头可能输出不同格式

3. **UI 进度条语义**：
   - 摄像头 duration=0，进度条显示运行时长
   - 进度条拖动对摄像头无效（已防护）

---

## 9. 后续计划

### Phase 2.2：设备枚举与参数选择
- 枚举系统摄像头列表
- 枚举支持的分辨率/帧率
- 用户选择摄像头和参数
- 保存上次使用的摄像头配置

### Phase 2.3：实时源语义接口优化
- 为 `IMediaSource` 添加能力查询接口：
  ```cpp
  virtual bool IsSeekable() const = 0;
  virtual bool HasDuration() const = 0;
  virtual bool IsRealtime() const = 0;
  ```
- UI 对实时源禁用进度条拖动
- UI 对实时源显示 "Live" 状态

### Phase 2.4+：功能扩展
- 摄像头截图
- 摄像头录像
- 摄像头音频接入
- 设备属性控制
- 多摄像头支持

---

## 10. 结论

Phase 2.1 C920 video-only MVP 已成功实现并通过测试验证。

**核心成果**：
1. ✅ 摄像头作为实时 `IMediaSource` 接入现有架构
2. ✅ 与文件视频共用播放管线和多路预览体系
3. ✅ 实时源 seek 防护机制生效
4. ✅ 视频格式归一化机制生效
5. ✅ 单路和混合多路预览均正常

**架构验证**：
- 现有架构可以平滑支持摄像头等实时源
- 无需为摄像头另起一套播放链路
- `IMediaSource` 抽象设计合理

**下一步**：可以继续推进 Phase 2.2 设备枚举与参数选择，或开始其他 Phase 任务。
