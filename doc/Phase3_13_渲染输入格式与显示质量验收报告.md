# Phase 3.13 渲染输入格式与显示质量验收报告

## 1. 验收结论

Phase 3.13 的代码层目标已经收口：统一渲染链路不再直接假设输入帧一定是 `YUV420P`，而是在 `MultiViewRenderer::PresentFrame()` 前通过 `VideoFrameConverter` 建立可显示帧边界。

当前结论分为两层：

- 静态验收：已完成。格式转换、色彩范围、纹理上传和显示矩形边界已经集中到基础设施层处理。
- 人工样本验收：仍需要本地运行确认。普通 SDR、非 `YUV420P`、4K MKV、疑似 HDR/BT.2020 样本的最终画面效果不能只靠静态检查断言。

Phase 3.13 可以作为进入 Phase 3.14 多路性能优化前的显示正确性基础，但建议在进入性能优化前至少完成一轮 4K MKV 和非 `YUV420P` 样本播放验收。

## 2. 当前渲染链路

Phase 3.13 后，视频帧显示链路保持为：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> IVideoFrameSink
            -> SingleStreamFrameSinkAdapter
                -> MultiViewRenderer
                    -> VideoFrameConverter
                    -> YUV420P 纹理上传
                    -> OpenGL shader 绘制
```

边界没有变化：

- `MediaPipeline` 仍只负责单路播放管线。
- `VideoFrameScheduler` 仍只负责取帧、同步判断和丢帧。
- `MultiViewRenderer` 不访问帧队列、同步器或播放管线。
- `VideoFrameConverter` 只负责把软件内存帧转换为当前 renderer 可绘制格式。
- `MainWindow`、`MultiStreamManager` 不承载像素格式和色彩转换逻辑。

## 3. 支持的输入格式范围

当前 renderer 的最终显示格式仍是：

```text
AV_PIX_FMT_YUV420P
```

但输入不再要求必须已经是 `YUV420P`。`VideoFrameConverter` 会把软件内存帧转换为当前可显示格式。

已覆盖的格式类型：

- `YUV420P`：如果已经符合当前显示色彩 profile，则原样通过；否则重新转换以统一显示 profile。
- `YUVJ420P` / `YUVJ422P` / `YUVJ444P` / `YUVJ440P` / `YUVJ411P`：按 JPEG/full range 策略处理。
- `NV12`、`YUV422P`、`YUV444P` 等常见软件帧格式：通过 swscale 转换到 `YUV420P`。
- `P010`、`YUV420P10LE` 等高位深格式：通过 swscale 降级到 8-bit `YUV420P`，不保证 HDR 或 10-bit 精确还原。

不在本阶段完整支持：

- 硬件帧直接渲染。硬件帧需要先由 decoder transfer 到系统内存。
- HDR tone mapping。
- BT.2020 / PQ / HLG 的专业级色彩还原。
- GPU shader 多格式直接渲染。

## 4. 色彩范围与色彩空间策略

Phase 3.13.4 已将色彩策略集中到 `VideoFrameConverter`。

当前策略：

- 输入 `AVCOL_RANGE_JPEG`：按 full range 处理。
- 输入 `AVCOL_RANGE_MPEG`：按 limited range 处理。
- `YUVJ*` 且未明确标记 range：按 full range 处理。
- 未知 range：默认按 limited range 处理，并输出低频诊断日志。
- 输入 `BT.709`：使用 `SWS_CS_ITU709`。
- 输入 `BT.601` / `SMPTE170M`：使用 `SWS_CS_SMPTE170M`。
- 未知 colorspace：宽度大于等于 1280 优先按 `BT.709`，否则按 `SMPTE170M`。
- `BT.2020`：降级为 SDR `BT.709` 输入策略，并输出限制日志。

输出 profile：

```text
format       = AV_PIX_FMT_YUV420P
color_range  = AVCOL_RANGE_JPEG
colorspace   = AVCOL_SPC_SMPTE170M
primaries    = AVCOL_PRI_SMPTE170M
transfer     = AVCOL_TRC_BT709
```

这个策略服务于当前 shader 的显示约束。它的目标是稳定显示常见 SDR 内容，不是完整色彩管理系统。

## 5. 4K MKV 显示修复结论

Phase 3.13 修复了两类 4K MKV 风险。

第一类是格式解释错误：

- decoder 不再把硬解 transfer 后的软件帧强行当作固定格式处理。
- `VideoFrameConverter` 使用真实 `AVFrame::format` 作为 swscale 输入格式。
- 非 `YUV420P` 帧统一转换到 renderer 可绘制格式。

第二类是纹理上传边界错误：

- `MultiViewRenderer` 新增 `YuvPlaneUpload`，明确 Y/U/V 三个 plane 的数据、可见宽高和 row length。
- 纹理尺寸使用 plane 可见尺寸，不使用 `linesize`。
- `linesize` 只作为 `QOpenGLPixelTransferOptions::setRowLength()`。
- U/V 平面尺寸使用向上取整，避免奇数宽高视频出错。
- 上传 alignment 设置为 1，避免默认 4 字节行对齐影响单通道纹理上传。
- `CalcFrameRect()` 使用正数 SAR 计算显示宽高比，异常 SAR 默认按 1:1 处理。

静态层面，4K 画面只显示局部的问题已经从“可能把 stride 当尺寸”这个根因上处理。最终是否完全关闭，需要用户用异常 4K MKV 样本运行验证。

## 6. 对核心架构的影响

本阶段没有改变主播放架构。

未改变：

- `MainWindow` 不直接处理像素格式。
- `MediaPlayController` 不处理渲染细节。
- `MultiStreamManager` 不感知帧格式。
- `MediaPipeline` 不引入 `StreamId`。
- `VideoFrameScheduler` 不做格式转换。
- `IVideoFrameSink` 仍只作为帧输出边界。

新增或强化：

- `VideoFrameConverter` 成为渲染前格式适配边界。
- `MultiViewRenderer` 明确只接收可显示帧，并负责 OpenGL 纹理上传和 viewport 绘制。
- `decoder` 保留真实软件帧格式，避免在解码层提前吞掉格式信息。

该设计符合当前分层路线：解码负责产出帧，调度负责时间，转换负责显示格式，renderer 负责绘制。

## 7. 静态检查记录

已完成的主要静态检查包括：

```powershell
git diff --check -- CMakeLists.txt src\infrastructure\decoder.cpp src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\infrastructure\videoframeconverter.h src\infrastructure\videoframeconverter.cpp doc\Phase3_13TODO.md
git diff --check -- src\infrastructure\decoder.h src\infrastructure\decoder.cpp src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\infrastructure\videoframeconverter.h src\infrastructure\videoframeconverter.cpp doc\Phase3_13TODO.md
git diff --check -- src\infrastructure\multiviewrenderer.cpp doc\Phase3_13TODO.md
```

关键搜索包括：

```powershell
rg -n "sws_|AV_PIX_FMT|color_range|linesize|VideoFrameConverter|PresentFrame" src\infrastructure doc\Phase3_13TODO.md
rg -n "color_range|color_space|BT|YUVJ|AVCOL|sws_setColorspaceDetails|m_swsCtx" src\infrastructure doc\Phase3_13TODO.md
rg -n "CalcFrameRect|DisplayAspectRatio|videoWidth|videoHeight|linesize|sampleAspectRatio|YuvPlaneUpload|setRowLength|setAlignment" src\infrastructure doc\Phase3_13TODO.md
```

## 8. 人工验收建议

建议按以下顺序验收：

1. 普通 `YUV420P` MP4：确认旧视频播放不退化。
2. 非 `YUV420P` 样本：确认不黑屏、不崩溃、画面基本正常。
3. `YUVJ420P` 或 full range 样本：确认不明显发灰或过曝。
4. 4K SDR MKV：确认不再只显示局部画面，比例正常。
5. 疑似 HDR / BT.2020 4K 视频：确认不崩溃，允许色彩降级。
6. 多路布局：确认 `Single`、`2x2`、`3x3`、`4x4` 下画面不越界。
7. 双击聚焦和恢复：确认 viewport 切换后纹理显示区域正确。

## 9. 残留限制

当前仍存在以下限制：

- HDR / BT.2020 / PQ / HLG 只做降级，不做完整还原。
- 高位深格式会降级到 8-bit `YUV420P`。
- 色彩策略是第一版 SDR 策略，不是完整 CMS。
- 多路性能开销可能增加，尤其是多路 4K 或高位深视频同时转换时。
- 真实异常样本的最终结论依赖人工运行结果。

## 10. 进入 Phase 3.14 的条件

可以进入 Phase 3.14 多路性能优化，但建议满足以下条件：

- 至少一个普通 SDR 视频播放正常。
- 至少一个非 `YUV420P` 样本播放不黑屏。
- 至少一个 4K MKV 样本不再出现明显局部显示。
- 已接受 HDR / BT.2020 当前只做降级的限制。

Phase 3.14 的重点应从“显示是否正确”切换到“多路显示时性能是否可观测、可解释、可优化”。
