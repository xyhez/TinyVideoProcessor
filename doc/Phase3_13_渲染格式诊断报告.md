# Phase 3.13 渲染格式诊断报告

## 1. 诊断目标

本报告对应 `Phase 3.13.1 问题样本与格式诊断收集`。

当前阶段先解决一个前置问题：主窗口已经全面转向 `MultiViewRenderer`，因此后续非 `YUV420P`、4K MKV、色彩异常和局部显示问题，都应优先从统一渲染链路采集诊断信息，而不是继续依赖旧 `VideoRender` fallback 的日志。

## 2. 当前静态结论

当前统一显示链路为：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> IVideoFrameSink
            -> SingleStreamFrameSinkAdapter
                -> MultiViewRenderer
```

职责边界保持不变：

- `MediaPipeline` 负责单路播放管线。
- `VideoFrameScheduler` 负责取帧、同步判断、丢帧和提交可显示帧。
- `IVideoFrameSink` 负责接收可显示帧。
- `MultiViewRenderer` 负责持有当前显示帧、纹理上传、viewport 绘制和 active 高亮。
- `MultiViewRenderer` 不访问 `VFrame_`，不访问 `SyncManager`，不做同步判断。

## 3. 当前支持范围

### 3.1 MultiViewRenderer 当前直接支持格式

静态检查确认：

```cpp
bool MultiViewRenderer::IsSupportedFrameFormat(const StreamRenderState& state) const {
    return state.currentFrame && state.pixelFormat == AV_PIX_FMT_YUV420P;
}
```

因此 `MultiViewRenderer` 当前直接支持：

- `AV_PIX_FMT_YUV420P`
- 8-bit planar Y / U / V 三纹理上传
- 按 `frame->width` / `frame->height` 创建纹理
- 按 `frame->linesize[0..2]` 设置 OpenGL row length
- 按 `frame->sample_aspect_ratio` 计算显示矩形

当前不直接支持：

- `NV12`
- `YUVJ420P`
- `YUV422P`
- `YUV444P`
- `P010` / 10-bit
- RGB/BGR 类格式
- HDR / BT.2020 / PQ 的完整显示还原

### 3.2 decoder 当前已有转换逻辑

静态检查发现，`decoder` 当前已经在视频帧输出前做了格式转换：

- 硬解输出如果是硬件帧，会先转系统内存。
- 硬解 `NV12` 会转换为 `YUV420P`。
- 软解视频帧如果不是 `YUV420P`，也会通过 `sws_scale()` 转成 `YUV420P`。

这意味着后续异常样本实际进入 `MultiViewRenderer` 时，可能已经不是原始解码格式，而是 decoder 转换后的 `YUV420P`。

这个点很关键：如果 4K MKV 颜色仍然不对，问题不一定是 `MultiViewRenderer` 不支持原始 pix_fmt，也可能是：

- decoder 转换阶段没有正确设置色彩范围 / 色彩空间。
- shader 固定使用一套 YUV -> RGB 公式。
- full range / limited range 未区分。
- BT.601 / BT.709 / BT.2020 未区分。
- HDR / 10-bit 被第一版转换策略降级后丢失信息。

### 3.3 已确认的硬解根因

用户已通过日志确认：当前异常只在硬解播放 4K MKV 时出现，硬解 transfer 出来的实际软件帧格式是类似 `yuv420p10le` 的 10-bit planar 格式，但旧代码在 `TransferHWFrame()` 中固定按 `NV12` 作为 `sws_scale()` 输入格式。

旧逻辑的问题是：

```text
av_hwframe_transfer_data()
    -> 得到实际 software frame
    -> 忽略 softwareFrame->format
    -> 固定使用 AV_PIX_FMT_NV12 调 sws_getCachedContext()
```

当实际格式不是 `NV12` 时，FFmpeg 会按错误的像素布局解释数据，结果就是：

- 颜色错误。
- 平面数据解释错误。
- 纹理上传前的 YUV420P 内容已经不可信。
- 后续 renderer 即使按 YUV420P 正确绘制，也只能显示错误结果。

本轮已修正为：

```text
av_hwframe_transfer_data()
    -> 读取 softwareFrame->format
    -> 如果已经是 YUV420P，则直接返回
    -> 否则按实际 sourceFormat 转换到 YUV420P
```

同时转换后的输出帧通过 `av_frame_copy_props()` 保留时间戳、SAR 和色彩元数据，避免转换阶段丢失后续渲染诊断所需信息。

## 4. 本轮新增诊断能力

本轮为 `MultiViewRenderer` 增加了格式签名变化日志。后续运行异常样本时，控制台会输出类似：

```text
MultiViewRenderer accepted frame format:
streamId=...
pix_fmt=...
size=...
linesize=[...]
sar=...
color_range=...
colorspace=...
color_primaries=...
color_trc=...
```

如果格式不支持，会输出：

```text
MultiViewRenderer unsupported frame format:
...
```

日志触发策略：

- 同一路 stream 的格式签名变化时输出。
- 支持状态变化时输出。
- 尺寸、linesize、SAR、色彩范围或色彩空间变化时输出。
- 不对每帧刷屏。

## 5. 异常类型判定方法

后续拿到样本日志后，按以下方式判断问题归属。

### 5.1 格式不支持

特征：

- 日志出现 `MultiViewRenderer unsupported frame format`
- `pix_fmt` 不是 `yuv420p`
- 画面黑屏或停留在背景色

归属：

- 第一优先处理可显示帧格式转换边界。
- 不应在 UI 层写特殊判断。
- 不应让 `VideoFrameScheduler` 负责格式转换。

### 5.2 色彩错误

特征：

- 日志出现 `MultiViewRenderer accepted frame format`
- `pix_fmt=yuv420p`
- 画面能显示完整内容，但发灰、过曝、偏色或肤色异常
- `color_range`、`colorspace`、`color_primaries`、`color_trc` 非默认组合

归属：

- 优先检查 `sws_scale()` 转换时的色彩空间设置。
- 检查 shader 是否固定使用不适合当前视频的 YUV -> RGB 矩阵。
- 对 4K SDR 常见视频，第一版应优先按 BT.709 处理。

### 5.3 显示矩形或局部显示错误

特征：

- 日志显示 `pix_fmt=yuv420p`
- 颜色可能正常或异常，但只能看到局部画面
- `size`、`linesize`、`sar` 与实际视频比例不匹配或 renderer 计算不正确

归属：

- 优先检查纹理尺寸是否使用可见宽高。
- 检查 `linesize` 是否只用于上传 row length，而不是误当显示宽高。
- 检查 `CalcFrameRect()` 的 SAR/DAR 计算。
- 检查高 DPI 下 Qt 逻辑坐标到 framebuffer 坐标转换。

## 6. 待用户补充的样本信息

3.13.1 尚未完全关闭，因为还缺真实异常样本运行日志。

请后续分别运行：

1. 一个非 `YUV420P` 或疑似格式异常的视频。
2. 一个 4K MKV 颜色异常或局部显示的视频。

每个样本记录：

- 文件名或简短标识。
- 是否硬解。
- 控制台中的 `MultiViewRenderer accepted/unsupported frame format` 日志。
- 现象：黑屏、颜色错误、比例错误、局部显示、卡顿。
- 是否有音频流。

## 7. 下一步建议

样本日志收集后，再进入 `3.13.2 渲染输入格式适配方案设计`。

下一步不建议直接改 shader 或直接在 `DrawStreamFrame()` 堆格式分支。更合理的方向是先确定可显示帧格式边界：当前 decoder 已经存在一部分转换逻辑，后续需要判断它是否应该保留、收口为 `VideoFrameConverter`，或迁移到 renderer 前的独立适配层。

如果硬解 4K MKV 在本轮修正后恢复正常，Phase 3.13 后续应继续做的是收口格式转换边界，而不是再围绕该文件追加特殊逻辑。
