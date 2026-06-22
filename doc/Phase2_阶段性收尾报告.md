# Phase 2 摄像头输入阶段性收尾报告

## 1. 收尾结论

Phase 2 当前可以阶段性收口。

本阶段已经完成摄像头输入的主链路目标：

- C920 video-only 摄像头源可作为 `IMediaSource` 接入。
- 摄像头可进入现有 `MediaPipeline`。
- 摄像头可进入现有多路预览体系。
- 摄像头无音频时不破坏 active audio stream 策略。
- 实时源不可 seek 语义已接入 Controller 和 UI。
- 打开失败、运行中异常、重复添加、释放链路已经具备代码闭环。

2.5 之后的配置持久化、截图、录像、摄像头音频、设备属性控制、多摄像头/工业相机/网络摄像头预研都属于扩展能力，不阻塞当前播放主链路。

## 2. 已完成主链路

### 2.1 C920 video-only MVP

已完成：

- `CameraMediaSource` 接入。
- dshow video-only 打开。
- 摄像头复用现有 demuxer / decoder / renderer 链路。
- 单路摄像头预览。
- 文件 + 摄像头混合多路预览。

结果：

- 摄像头不需要专用播放管线。
- 文件播放路径不退化。
- 摄像头无音频时不创建音频解码和播放链路。

### 2.2 设备枚举与参数选择

已完成：

- 摄像头参数数据结构。
- `CameraDeviceEnumerator` 设备枚举服务。
- 摄像头设置对话框。
- UI 通过 `MediaPlayController::AddStream()` 添加摄像头。

保留限制：

- 设备能力枚举仍使用 C920 已知格式列表。
- 非 C920 设备的真实能力动态解析尚未完成。

该限制不影响当前 C920 主链路验证。

### 2.3 实时源语义完善

已完成：

- `IMediaSource::IsSeekable()`。
- `IMediaSource::HasDuration()`。
- `IMediaSource::IsRealtime()`。
- Controller 和 UI 基于 active stream 能力判断 seek 和 duration。
- 实时源显示 `LIVE`，禁用进度条拖动。

结果：

- 文件源和摄像头源不再依赖临时类型判断。
- 后续网络流也可以复用同一套能力语义。

### 2.4 异常与生命周期

已完成：

- 实时源 Stop 不再执行 seek 回零。
- `IMediaSource::LastError()` 支持打开失败原因上报。
- 摄像头打开失败链路可转发到 UI。
- `demuxer::DemuxErrorOccurred()` 支持运行期读包错误上报。
- 运行期错误复用 `MediaPipeline -> MultiStreamManager -> MediaPlayController -> UI` 链路。
- `IMediaSource::SourceIdentity()` 和 `AllowsDuplicateOpen()` 支持重复添加防护。
- 摄像头释放链路已静态确认闭环。

保留实机验证项：

- 设备被占用、参数不支持等失败场景。
- 运行中拔出摄像头。
- 异常后 remove stream 再次添加。
- 关闭程序不阻塞。

## 3. 暂缓范围

以下内容暂缓，不删除：

- Phase 2.5 摄像头配置持久化。
- Phase 2.6 摄像头截图。
- Phase 2.7 摄像头录像预研。
- Phase 2.8 摄像头音频预研。
- Phase 2.9 设备属性控制预研。
- Phase 2.10 多摄像头、工业相机与网络摄像头预研。

暂缓原因：

- 这些能力不是当前播放主链路必需项。
- 继续推进会拉长 Phase 2，并分散当前对多路显示和渲染质量的投入。
- 工业相机、网络摄像头、设备属性控制需要更明确设备和业务场景后再设计。

## 4. 架构边界状态

当前 Phase 2 没有破坏既定架构边界：

- `MainWindow` 不直接管理摄像头 FFmpeg 上下文。
- `MediaPlayController` 继续作为 UI 层播放入口。
- `MultiStreamManager` 只按 `StreamId` 管理 stream。
- `MediaPipeline` 保持单路播放管线职责。
- `CameraMediaSource` 只负责摄像头媒体源打开、关闭和自身能力声明。
- 设备枚举、UI 参数选择、异常策略、重复添加策略、释放策略均未塞入 `CameraMediaSource`。

## 5. 当前已知限制

- 摄像头能力枚举未完全动态化。
- 摄像头身份当前基于设备名，多个同名设备需要后续独立处理。
- 运行期异常阈值是固定连续错误次数。
- 摄像头音频、截图、录像、设备属性控制暂未实现。
- 工业相机和网络摄像头仅保留架构预研入口。

## 6. 下一阶段建议

建议暂时停止 Phase 2 扩展项，回到更核心的显示和渲染主线。

优先方向：

- CAD/3D 场景显示效果对齐。
- `VideoRender` / 渲染模块能力完善。
- `MultiViewRenderer` 后续真实接入策略。
- 多路显示体验和性能收口。

Phase 2 后续如果恢复，建议从明确需求开始：

- 如果只是提升体验，优先恢复 2.5 配置持久化。
- 如果要做生产能力，优先预研 2.7 录像。
- 如果要接工业场景，优先重开 2.10 工业相机边界。

## 7. 结论

Phase 2 已完成摄像头输入主链路和生命周期基础能力，可以阶段性收尾。

后续应把精力转向当前更核心的多路显示、CAD/3D 渲染质量和渲染架构完善。
