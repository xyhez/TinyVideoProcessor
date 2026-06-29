# TODO: Phase 3.13 渲染输入格式与显示质量完善

## 当前进度

- 已完成：
  - Phase 3.11 已将主窗口常规视频显示区域统一到 `MultiViewRenderer`。
  - Phase 3.12 已完成 `Single` / `2x2` / `3x3` / `4x4` 布局与双击聚焦交互。
  - 当前 `MultiViewRenderer` 已能绘制基础 `YUV420P` 帧。
  - Phase 3.13.1 已为 `MultiViewRenderer` 补充格式签名诊断日志，并形成静态诊断报告。
  - Phase 3.13.2 已完成渲染输入格式适配方案设计，推荐新增 `VideoFrameConverter` 收口可显示帧格式边界。
  - Phase 3.13.3 已修正硬解 transfer 后固定按 `NV12` 转换的问题，改为按实际软件帧格式转换。
  - Phase 3.13.3 已新增 `VideoFrameConverter`，并在 `MultiViewRenderer::PresentFrame()` 前归一化可显示帧格式。
- 未完成：
  - 真实异常样本日志尚未收集，需要人工运行非 `YUV420P` / 4K MKV 样本后补充。
  - 非 `YUV420P` 视频帧的稳定显示。
  - 4K MKV 颜色异常和画面只显示局部的问题。
  - 色彩范围、色彩空间、像素格式和显示矩形的系统性处理。
- 已知风险：
  - 如果直接在 `MultiViewRenderer::DrawStreamFrame()` 中堆格式判断，会形成补丁式渲染逻辑。
  - 如果把格式转换塞进 `VideoFrameScheduler`，会破坏调度器只负责取帧、同步判断和丢帧的边界。
  - 4K MKV 可能同时包含像素格式、linesize、色彩范围、SAR/DAR、HDR/10bit 等多个问题，不能只按一个现象修。
- 本阶段范围：
  - 解决渲染输入格式和显示质量问题。
  - 不做多路性能优化。
  - 不扩展 9/16 路真实播放容量。

## 阶段目标

让统一渲染路径可以正确处理常见视频帧输入格式，并修复 4K MKV 画面颜色异常、画面比例异常和画面显示不完整问题。

目标链路保持：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> IVideoFrameSink
            -> SingleStreamFrameSinkAdapter
                -> MultiViewRenderer
```

本阶段允许增强：

- 解码后视频帧格式识别。
- 渲染前格式适配或转换。
- `MultiViewRenderer` 的纹理上传和 shader 输入约束。
- 帧显示矩形、SAR/DAR 和 linesize 处理。

本阶段不改变：

- `MediaPipeline` 单路播放职责。
- `VideoFrameScheduler` 同步调度职责。
- `MultiStreamManager` stream 生命周期职责。
- `MainWindow` 的逻辑 slot 分配职责。

## 架构边界

- 不把 `StreamId` 放进 `MediaPipeline`。
- 不把 `StreamId` 放进 `VideoFrameScheduler`。
- 不让 `MultiViewRenderer` 访问 `VFrame_` 或 `SyncManager`。
- 不让 `MultiViewRenderer` 做取帧、同步判断、丢帧或队列消费。
- 不让格式兼容逻辑散落在 UI 层。
- 不为了单个 4K MKV 文件写特殊分支。
- 优先形成可复用的帧格式适配边界。

## 参考文档

- `doc/架构指导.md`
- `doc/TODO.md`
- `doc/Phase3_5_音视频格式归一化预研报告.md`
- `doc/Phase3_7_视频帧输出边界设计.md`
- `doc/Phase3_7_帧所有权与清理协议.md`
- `doc/Phase3_9_MultiViewRenderer多slot真实帧扩展验收报告.md`
- `doc/Phase3_12_MultiViewRenderer布局与交互验收报告.md`

## 3.13.1 问题样本与格式诊断收集

**目标**：先把出问题的视频信息收集清楚，避免凭现象猜测。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `doc/Phase3_13TODO.md`
- 可选：`doc/Phase3_13_渲染格式诊断报告.md`

**任务清单**：

- [ ] 收集至少一个非 `YUV420P` 显示异常样本。
- [ ] 收集至少一个 4K MKV 显示异常样本。
- [ ] 记录视频 codec、pix_fmt、width、height、linesize、color_range、color_space、color_primaries、color_trc。
- [ ] 记录 SAR/DAR 信息。
- [ ] 记录是否为硬解输出帧、是否经过硬解转系统内存。
- [x] 为 `MultiViewRenderer` 增加格式签名变化诊断日志。
- [x] 明确当前 `MultiViewRenderer` 支持格式范围。
- [ ] 明确异常属于黑屏、颜色错误、比例错误、裁切错误还是局部显示。

**验收标准**：

- [ ] 能说明每个异常样本的真实帧格式。
- [ ] 能说明当前渲染器为什么不能正确显示该样本。
- [ ] 能区分格式不支持、色彩转换错误和显示矩形错误。

**测试流程**：

- [ ] 本任务主要为诊断和文档，不要求编译。
- [x] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_13TODO.md doc\Phase3_13_渲染格式诊断报告.md`
- [ ] 人工验证建议：运行异常样本，记录控制台帧格式日志。

**Commit message**：

```text
docs: 梳理渲染格式异常样本

- 为 MultiViewRenderer 增加格式诊断日志
- 梳理非 YUV420P 和 4K MKV 显示异常样本信息
- 记录帧像素格式、色彩信息、linesize 和 SAR/DAR
- 区分格式不支持、色彩错误和显示矩形错误
- 为后续渲染格式适配提供诊断依据
```

## 3.13.2 渲染输入格式适配方案设计

**目标**：确定格式适配应该放在哪里，以及第一版支持哪些格式。

**预计修改文件**：

- `doc/Phase3_13_渲染输入格式适配方案.md`
- `doc/Phase3_13TODO.md`

**任务清单**：

- [x] 对比三种方案：解码器输出统一格式、sink 适配格式、渲染器内部格式适配。
- [x] 明确第一版推荐方案：渲染前建立统一可显示帧格式边界。
- [x] 明确是否新增类似 `VideoFrameConverter` 的基础设施类。
- [x] 明确第一版目标格式是否统一转为 `YUV420P` 或 `RGB`。
- [x] 明确 `NV12`、`YUVJ420P`、`YUV422P`、`P010` 的处理策略。
- [x] 明确 `AVFrame*` 转换后的所有权归属。
- [x] 明确转换失败时的日志和降级行为。
- [x] 明确不在本阶段引入 GPU shader 多格式矩阵。

**验收标准**：

- [x] 能说明格式转换不属于 UI。
- [x] 能说明为什么不把转换塞进 `VideoFrameScheduler`。
- [x] 能说明转换前后 `AVFrame*` 谁负责释放。
- [x] 能说明第一版支持格式和暂不支持格式。

**测试流程**：

- [x] 本任务为方案文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_13_渲染输入格式适配方案.md doc\Phase3_13TODO.md`

**Commit message**：

```text
docs: 设计渲染输入格式适配方案

- 新增 Phase 3.13 渲染输入格式适配方案
- 对比解码器统一输出、sink 适配和渲染器内部适配方案
- 明确格式转换边界不进入 VideoFrameScheduler
- 明确第一版支持格式、降级行为和 AVFrame 所有权语义
```

## 3.13.3 可显示帧格式转换最小实现

**目标**：实现第一版通用格式转换，不按单个文件打补丁。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `src/infrastructure/decoder.cpp`
- 可选：`src/infrastructure/videoframeconverter.h`
- 可选：`src/infrastructure/videoframeconverter.cpp`
- `CMakeLists.txt`
- `doc/Phase3_13TODO.md`

**任务清单**：

- [x] 新增或收口可显示帧格式转换入口。
- [x] 修正硬解 transfer 后误把实际软件帧格式固定当作 `NV12` 的问题。
- [x] 硬解转换路径使用 `AVFrame::format` 作为 `sws_getCachedContext()` 输入格式。
- [x] 硬解转换输出帧复制原帧时间戳、SAR 和色彩元数据。
- [x] 支持 `YUV420P` 原样通过。
- [x] 支持 `YUVJ420P` 等格式转换到当前 renderer 可绘制格式，full range 精确显示留到 3.13.4。
- [x] 支持常见非 `YUV420P` 格式转换到当前 renderer 可绘制格式。
- [x] 转换时正确处理 `linesize`。
- [x] 转换失败时释放中间资源，不泄漏。
- [x] 保持 `PresentFrame()` 所有权语义清晰。
- [x] 避免每帧重复创建昂贵上下文，必要时缓存转换上下文。

**验收标准**：

- [ ] `YUV420P` 旧视频播放不退化。
- [ ] 非 `YUV420P` 样本不黑屏。
- [x] 不支持格式有明确日志，不崩溃。
- [x] `AVFrame*` 不重复释放、不泄漏。

**测试流程**：

- [x] 静态检查：`git diff --check -- CMakeLists.txt src\infrastructure\decoder.cpp src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\infrastructure\videoframeconverter.h src\infrastructure\videoframeconverter.cpp doc\Phase3_13TODO.md`
- [x] 定向搜索：`rg -n "sws_|AV_PIX_FMT|color_range|linesize|VideoFrameConverter|PresentFrame" src\infrastructure doc\Phase3_13TODO.md`
- [ ] 人工运行建议：普通 MP4、非 `YUV420P` 视频、4K MKV、无音频视频。

**Commit message**：

```text
feat: 增加渲染输入格式转换能力

- 为 MultiViewRenderer 增加可显示帧格式适配入口
- 支持常见非 YUV420P 帧转换到当前可绘制格式
- 正确处理 linesize 和 AVFrame 所有权
- 保持 VideoFrameScheduler 只负责调度不负责格式转换
- 为 4K MKV 和多格式视频显示修复打基础
```

## 3.13.4 色彩范围与色彩空间处理

**目标**：修复颜色发灰、过曝、偏色等色彩问题。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- 可选：`src/infrastructure/videoframeconverter.h`
- 可选：`src/infrastructure/videoframeconverter.cpp`
- `doc/Phase3_13TODO.md`

**任务清单**：

- [ ] 明确 limited range 和 full range 的处理方式。
- [ ] 处理 `YUVJ420P` / JPEG range 场景。
- [ ] 明确 BT.601、BT.709 的第一版策略。
- [ ] 对 4K 视频优先按 BT.709 处理。
- [ ] 对未知色彩信息使用稳定默认值，并输出一次性日志。
- [ ] 不在本步完整支持 HDR tone mapping。
- [ ] 不在本步支持 BT.2020 / PQ 的完整显示还原。

**验收标准**：

- [ ] 常见 SDR 视频颜色正常。
- [ ] `YUVJ420P` 不明显发灰或过曝。
- [ ] 4K SDR MKV 颜色不再明显错误。
- [ ] HDR/BT.2020 未完整支持时有明确限制说明。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\infrastructure\videoframeconverter.h src\infrastructure\videoframeconverter.cpp doc\Phase3_13TODO.md`
- [ ] 定向搜索：`rg -n "color_range|color_space|BT|YUVJ|AVCOL|sws_setColorspaceDetails" src\infrastructure doc\Phase3_13TODO.md`
- [ ] 人工运行建议：分别播放 SDR 720p、1080p、4K MKV、疑似 full range 视频。

**Commit message**：

```text
fix: 完善视频帧色彩范围处理

- 处理 limited range 和 full range 视频帧显示差异
- 为 YUVJ420P 和常见 SDR 视频建立稳定色彩策略
- 对未知色彩信息使用默认降级并输出诊断日志
- 保持 HDR 和 BT.2020 完整支持作为后续限制
```

## 3.13.5 4K 显示矩形与纹理上传修正

**目标**：修复 4K MKV 只能看到部分画面、比例异常或纹理上传区域错误的问题。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- 可选：`src/infrastructure/videorender.h`
- 可选：`src/infrastructure/videorender.cpp`
- `doc/Phase3_13TODO.md`

**任务清单**：

- [ ] 复核 `videoWidth` / `videoHeight` 与 `linesize` 的使用边界。
- [ ] 确保纹理尺寸使用可见宽高，而不是错误使用 stride。
- [ ] 复核 `CalcFrameRect()` 对 SAR/DAR 的处理。
- [ ] 确保 4K 帧在不同 viewport 中保持完整可见。
- [ ] 确认高 DPI 下 framebuffer 坐标不影响 4K 纹理显示。
- [ ] 确认多路布局中 4K 视频不会越界绘制到其他 slot。

**验收标准**：

- [ ] 4K MKV 不再只显示局部画面。
- [ ] 画面比例符合视频 DAR。
- [ ] `Single`、`2x2`、`3x3`、`4x4` 下显示区域正确。
- [ ] 修复不影响普通 720p/1080p 视频。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\infrastructure\videorender.h src\infrastructure\videorender.cpp doc\Phase3_13TODO.md`
- [ ] 定向搜索：`rg -n "CalcFrameRect|videoWidth|videoHeight|linesize|sampleAspectRatio|glTexImage|glTexSubImage" src\infrastructure doc\Phase3_13TODO.md`
- [ ] 人工运行建议：4K MKV 单路播放、多路播放、布局切换、双击聚焦恢复。

**Commit message**：

```text
fix: 修正 4K 视频显示矩形和纹理上传

- 复核视频可见宽高、linesize 和纹理尺寸的使用边界
- 修正 4K MKV 画面只显示局部的问题
- 保持 SAR/DAR 下的画面比例计算正确
- 验证多布局下 4K 画面不越界绘制
```

## 3.13.6 Phase 3.13 验收报告

**目标**：收口渲染输入格式与显示质量阶段，记录支持范围和已知限制。

**预计修改文件**：

- `doc/Phase3_13_渲染输入格式与显示质量验收报告.md`
- `doc/Phase3_13TODO.md`

**任务清单**：

- [ ] 记录最终支持的像素格式范围。
- [ ] 记录 4K MKV 修复结论。
- [ ] 记录色彩范围和色彩空间策略。
- [ ] 记录暂不支持 HDR / BT.2020 完整还原的原因。
- [ ] 记录对 `MultiViewRenderer`、`VideoFrameScheduler`、`MediaPipeline` 边界的影响。
- [ ] 给出进入 Phase 3.14 性能优化的前置条件。

**验收标准**：

- [ ] 能说明非 `YUV420P` 问题是否关闭。
- [ ] 能说明 4K MKV 色彩和画面问题是否关闭。
- [ ] 能说明还剩哪些高阶格式未支持。
- [ ] 能说明下一阶段性能优化的观测指标。

**测试流程**：

- [ ] 本任务为验收报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_13_渲染输入格式与显示质量验收报告.md doc\Phase3_13TODO.md`

**Commit message**：

```text
docs: 增加渲染输入格式与显示质量验收报告

- 记录 Phase 3.13 支持的像素格式和显示质量修复结果
- 记录 4K MKV 色彩与画面显示问题的处理结论
- 说明 HDR 和 BT.2020 完整支持仍作为后续限制
- 明确进入多路性能优化阶段的前置条件
```

## 不做范围

- 不扩展真实播放路数到 9/16 路。
- 不做多路性能优化。
- 不实现 HDR tone mapping。
- 不实现网络流。
- 不实现图像处理算法。
- 不改变 active audio 策略。
- 不删除 `VideoRender` fallback。

## 风险与沟通点

- 如果异常 4K MKV 是 HDR / BT.2020 / 10bit 内容，第一版可能只能做到“不错误裁切、不崩溃、颜色有明确降级”，不能保证专业级色彩还原。
- 如果格式转换开销明显增加，需要在 Phase 3.14 中评估缓存、复用、异步上传或 GPU shader 多格式路径。
- 如果新增转换类影响 `CMakeLists.txt`，需要保持新增文件只属于基础设施层，不向 UI 泄漏 FFmpeg 转换细节。
