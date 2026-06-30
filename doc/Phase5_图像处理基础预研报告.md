# Phase 5 图像处理基础预研报告

## 阶段目标

Phase 5 的目标是在现有多路视频预览平台上添加基础实时图像处理能力。

本阶段不是直接做复杂检测算法，而是先建立一条稳定、可扩展、可关闭的图像处理链路，让后续灰度化、二值化、边缘检测、形态学和 Phase 6 目标检测可以沿同一套边界继续推进。

推荐第一版目标：

```text
只处理 active stream，先支持基础处理后预览，不承诺多路同时处理。
```

原因：

- 当前项目已经支持 9/16 路真实播放，继续对所有路同时处理会直接放大 CPU 压力。
- 图像处理第一阶段的核心价值是建立架构边界，而不是追求算法数量。
- active stream 处理更贴近用户操作，也更容易验收。
- 后续需要多路处理时，可以在处理链路稳定后再扩展到每路独立配置。

## 当前播放与渲染链路

当前主视频链路已经演进为：

```text
IMediaSource
    -> MediaPipeline
        -> demuxer
        -> decoder
        -> VFrame_
        -> VideoFrameScheduler
        -> IVideoFrameSink
        -> SingleStreamFrameSinkAdapter
        -> MultiViewRenderer
```

关键边界：

- `MediaPipeline` 负责单路播放管线生命周期。
- `VideoFrameScheduler` 负责取帧、同步判断、丢帧追赶和提交可显示帧。
- `IVideoFrameSink` 是调度器和显示端之间的边界。
- `SingleStreamFrameSinkAdapter` 负责把单路 sink 语义适配到带 `StreamId` 的 `MultiViewRenderer`。
- `MultiViewRenderer` 只负责 slot 布局、OpenGL 上传和绘制，不负责同步、取帧、解码或处理算法。
- `VideoFrameConverter` 已经承担“渲染器可显示格式”的转换职责。

## 图像处理应插入的位置

推荐插入位置：

```text
VideoFrameScheduler
    -> IVideoFrameSink
        -> ImageProcessingFrameSink
            -> ImageProcessorPipeline
            -> downstream IVideoFrameSink
                -> SingleStreamFrameSinkAdapter
                    -> MultiViewRenderer
```

也就是在“调度器已经决定这一帧可以显示”之后、进入最终渲染 sink 之前做处理。

这样做的原因：

- 不影响解复用和解码。
- 不让处理模块参与音视频同步判断。
- 不让 `MultiViewRenderer` 感知处理器链。
- 不需要 `MainWindow` 直接操作帧数据。
- 后续可以通过装饰器方式开启或关闭处理链路。

不推荐的位置：

### 1. 放在 decoder 后、队列前

问题：

- 处理耗时会阻塞解码线程或增加解码后队列压力。
- 处理后的帧可能改变时间戳和格式，影响后续同步判断。
- 多路场景下会把处理压力提前放大。

### 2. 放在 VideoFrameScheduler 内部

问题：

- 调度器职责会从“同步和取帧”扩散到“图像算法执行”。
- 算法耗时会干扰视频帧调度节奏。
- 后续算法参数、开关、性能统计会污染同步模块。

### 3. 放在 MultiViewRenderer 内部

问题：

- 渲染器会从“显示后端”变成“处理 + 显示 + 状态管理”混合模块。
- OpenGL 绘制和 OpenCV CPU 处理职责混杂。
- 后续想替换处理链或渲染后端会困难。

### 4. 放在 MainWindow 内部

问题：

- UI 会直接触碰视频帧和算法细节，违反现有分层。
- 多路状态、active stream、处理参数会变得难维护。

## 推荐新增抽象

### 1. IImageProcessor

职责：

- 表示一个可插拔的图像处理器。
- 只处理图像数据，不管理播放、同步、渲染、UI 生命周期。

第一版接口建议：

```cpp
class IImageProcessor {
public:
    virtual ~IImageProcessor() = default;
    virtual QString Name() const = 0;
    virtual bool Enabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;
    virtual bool Process(cv::Mat& image) = 0;
};
```

第一版可以让 `Process()` 原地修改 `cv::Mat`，减少拷贝和所有权复杂度。

### 2. ImageProcessorPipeline

职责：

- 管理多个 `IImageProcessor`。
- 按顺序执行已启用处理器。
- 统计处理耗时。
- 提供清空、添加、启停能力。

不负责：

- 不打开媒体源。
- 不访问 `SyncQueue`。
- 不访问 `SyncManager`。
- 不操作 OpenGL 纹理。
- 不决定 active stream。

### 3. VideoFrameToMatConverter

职责：

- 在 `AVFramePtr` 和 `cv::Mat` 之间建立转换边界。
- 第一版可以只支持当前显示链路稳定输出的 `YUV420P` / `BGR24` 中间格式。
- 统一处理 linesize、宽高、像素格式和输出帧属性复制。

注意：

当前 `VideoFrameConverter` 的职责是把各种视频帧转换为 renderer 可显示格式。Phase 5 如果需要 OpenCV 处理，不应直接复用它来承担 `cv::Mat` 转换职责，否则会把“显示格式转换”和“算法输入转换”混在一起。

### 4. ImageProcessingFrameSink

职责：

- 实现 `IVideoFrameSink`。
- 接收 `VideoFrameScheduler` 提交的可显示帧。
- 判断是否启用图像处理。
- 启用时调用 `ImageProcessorPipeline`。
- 将处理后的帧继续交给下游 sink。
- 未启用时直接把原帧转发给下游 sink。

推荐链路：

```text
VideoFrameScheduler
    -> ImageProcessingFrameSink
        -> SingleStreamFrameSinkAdapter
            -> MultiViewRenderer
```

这样可以保持 `VideoFrameScheduler` 和 `MultiViewRenderer` 的职责不变。

## OpenCV 依赖判断

`CMakeLists.txt` 当前没有接入 OpenCV。

Phase 5 需要正式引入 OpenCV，但建议分两步：

1. 先做环境和构建接入预检查，确认本机 OpenCV 路径、版本和 Qt / MinGW / MSVC ABI 是否一致。
2. 再新增最小处理器，不在依赖未确认时批量写算法。

原因：

- OpenCV 是新依赖，最容易出现 ABI、Debug/Release、编译器版本不一致问题。
- 项目当前同时出现过 MSVC 和 MinGW 构建目录，必须明确当前主构建链。
- 依赖接入失败时，不应影响已有播放器主线。

## 第一版处理范围

建议第一版只做：

- 灰度化。
- 二值化。
- 边缘检测。
- 处理开关。
- active stream 处理预览。
- 处理耗时日志或简单指标。

暂不做：

- 多路同时处理。
- 每路独立处理器链。
- 复杂参数面板。
- 处理器拖拽排序。
- GPU 图像处理。
- 目标检测。
- 测量和标定。
- 截图保存处理结果。

## UI 边界建议

第一版 UI 只需要能验证处理链路，不需要一次性做完整算法工作台。

推荐最小 UI：

- 一个“图像处理”开关。
- 一个处理模式选择，例如 `原图 / 灰度 / 二值化 / 边缘`。
- 二值化阈值滑块。
- 可选：显示当前处理耗时。

UI 只调用 `MediaPlayController` 或应用层接口，不直接访问处理器对象和帧数据。

推荐控制链路：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> active MediaPipeline
                -> ImageProcessingFrameSink / ImageProcessorPipeline
```

## active stream 策略

第一版只处理 active stream。

行为建议：

- 切换 active stream 后，处理开关仍然保持全局 UI 状态。
- 新的 active stream 如果支持视频，则使用同一处理配置。
- 非 active stream 保持原图预览，避免多路处理造成压力。
- 如果 active stream 被移除，则处理链路自动失效，不崩溃。

后续如果需要每路独立处理，再扩展为：

```text
StreamId -> ImageProcessorPipeline
```

但第一版不建议直接做。

## 帧格式与所有权

第一版必须保持现有 `AVFramePtr` 所有权模型：

- `VideoFrameScheduler` 把 `AVFramePtr` 移交给 sink。
- `ImageProcessingFrameSink` 接管输入帧。
- 如果不处理，直接移动给下游 sink。
- 如果处理，创建新的输出 `AVFramePtr`，输入帧由 RAII 自动释放。
- 转换失败时可以丢弃当前帧，但不能中断播放管线。

禁止：

- 不使用裸 `AVFrame*` 作为处理结果所有权。
- 不把 `cv::Mat` 持有的视频帧内存延迟交给异步线程使用。
- 不让处理器保存当前帧指针。

## 性能策略

第一版性能策略：

- 默认关闭图像处理。
- 只处理 active stream。
- 处理失败或耗时过高不阻塞关闭流程。
- 日志低频输出，避免每帧刷屏。
- 不承诺 9/16 路同时图像处理。

需要观察的指标：

- 单帧处理耗时。
- 处理后渲染是否卡顿。
- CPU 占用变化。
- 内存是否持续增长。
- 关闭程序是否阻塞。

## 风险

### 1. OpenCV ABI 风险

风险：

- OpenCV 编译器版本和当前 Qt kit 不一致，导致链接或运行失败。

处理：

- 先单独做 OpenCV 接入验证，不和算法开发混在一起。

### 2. 格式转换成本风险

风险：

- `AVFrame -> cv::Mat -> AVFrame` 会增加 CPU 和内存带宽压力。

处理：

- 第一版只处理 active stream。
- 后续再考虑转换缓存、降帧处理或 GPU 路径。

### 3. 渲染颜色回归风险

风险：

- OpenCV 默认 BGR，当前 renderer 主要消费 YUV420P，如果转换链不清晰，可能再次出现颜色异常。

处理：

- 明确 `VideoFrameToMatConverter` 职责。
- 保留 `av_frame_copy_props()` 或等价属性复制。
- 第一版重点验证普通 SDR 视频。

### 4. 关闭阻塞风险

风险：

- 图像处理耗时如果跑在 UI 线程或无法中断，会影响关闭。

处理：

- 第一版处理在帧提交链路内保持同步但小范围。
- 不引入后台处理线程。
- 后续如果引入异步处理，必须单独设计生命周期。

## 推荐实施顺序

### Phase 5.1：预研和任务拆分

产出：

- `doc/Phase5_图像处理基础预研报告.md`
- `doc/Phase5TODO.md`

### Phase 5.2：OpenCV 依赖接入预检查

目标：

- 确认当前工程如何引入 OpenCV。
- 不写图像处理逻辑。

### Phase 5.3：图像处理抽象空壳

目标：

- 新增 `IImageProcessor`、`ImageProcessorPipeline` 等基础抽象。
- 不接入播放链路。

### Phase 5.4：active stream 处理链路接入

目标：

- 通过 `ImageProcessingFrameSink` 把处理链路插到 `VideoFrameScheduler` 和下游 sink 之间。
- 默认关闭处理，不影响现有播放。

### Phase 5.5：基础处理器

目标：

- 实现灰度化、二值化、边缘检测。
- 先保证功能正确，不做复杂 UI。

### Phase 5.6：最小 UI 接入

目标：

- MainWindow 通过 Controller 控制处理开关和处理模式。
- UI 不接触帧数据。

### Phase 5.7：性能和关闭链路验收

目标：

- 验证处理开关、播放、暂停、停止、seek、remove、close。
- 记录处理耗时和 CPU 变化。

## 阶段结论

Phase 5 可以开始，但第一步必须先建立清晰处理边界。

推荐路线是：

```text
active stream
    -> VideoFrameScheduler
    -> ImageProcessingFrameSink
    -> ImageProcessorPipeline
    -> MultiViewRenderer
```

这条路线符合当前项目架构：

- 不改变媒体源抽象。
- 不破坏单路 `MediaPipeline` 职责。
- 不让调度器承担算法。
- 不让渲染器承担处理。
- 不让 UI 触碰帧数据。

因此，Phase 5 可以从 OpenCV 接入预检查和图像处理空壳开始推进。
