# Phase 3.14 预览降帧验收报告

## 验收结论

Phase 3.14.5 用户可控预览降帧判定通过。

本轮实现满足当前阶段目标：用户可以在主窗口选择“原始帧率”或“降帧预览”；默认保持原始帧率，不改变既有播放体验；开启降帧预览后，多路高帧率视频场景下 CPU 和 GPU 占用有可观察下降。

## 验收背景

当前多路播放已经统一进入 `MultiViewRenderer`，单路视频帧由 `VideoFrameScheduler` 负责取帧、同步判断、丢帧和提交到 `IVideoFrameSink`。

前一次性能观测中，4 路混合视频包含两路 `1920x1200@120fps` 高帧率源。此类源会显著增加解码、硬件帧下载、格式转换、纹理上传和绘制压力。为了避免在 `MultiViewRenderer` 中补丁式丢帧，本轮把预览降帧策略放在 `VideoFrameScheduler`，保持职责边界：

- `VideoFrameScheduler`：负责取帧、同步、预览降帧判断、提交显示帧。
- `MultiViewRenderer`：只负责接收已调度帧、纹理上传和绘制。
- `MediaPlayController`：保存用户级降帧开关并下发给全部流。
- `MainWindow`：只负责 UI 单选项和用户操作转发。

## 实现范围

本轮涉及文件：

- `src/infrastructure/videoframescheduler.h`
- `src/infrastructure/videoframescheduler.cpp`
- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `doc/Phase3_14TODO.md`

## 功能验收

### 1. 用户可控

主窗口新增两个单选项：

- `原始帧率`
- `降帧预览`

默认选中 `原始帧率`，不会主动改变现有播放行为。用户选中 `降帧预览` 后，`MediaPlayController` 将设置下发到所有已存在流；后续新增流也会继承当前设置。

### 2. 架构边界

降帧策略未放入 `MultiViewRenderer`，没有让渲染器参与取帧、同步或播放状态判断，符合当前架构路线。

当前链路为：

```text
MainWindow
  -> MediaPlayController
  -> MultiStreamManager
  -> MediaPipeline
  -> VideoFrameScheduler
  -> IVideoFrameSink
  -> MultiViewRenderer
```

### 3. 同步语义

预览降帧基于视频帧 PTS 间隔判断，而不是简单按调用次数跳过帧。

对有音频流的视频：

- 音频仍作为主时钟。
- 被跳过的预览帧不会提交给渲染器。
- 后续可显示帧仍经过原有同步判断。

对无音频流的视频：

- `VideoFrameScheduler` 在丢弃预览帧时仍会按帧 PTS 推进视频时钟。
- 避免因为跳过显示帧导致无音频视频整体变慢。

## 性能验收

用户实测结果：

```text
CPU 约降低 3%
GPU 约降低 5%
```

该结果说明当前策略有效，但收益有限。

收益有限的原因是：本轮降帧发生在 `VideoFrameScheduler`，位置在解码之后、提交渲染之前。因此它主要减少：

- `VideoFrameConverter` 格式转换次数
- `MultiViewRenderer` 纹理上传次数
- OpenGL 绘制压力
- UI 刷新压力

但它不能减少：

- demux 读包
- decoder 解码
- 硬解码后的硬件帧下载
- 解码输出前的高帧率处理压力

因此，本轮优化可以降低渲染后段压力，但不能解决高帧率源在解码/硬件帧转换前段产生的主要资源消耗。

## 后续优化项

如果后续要支撑 9/16 路真实播放，建议单独设计“解码输出/硬件帧转换前降载”专题。

候选方向：

- 在解码输出后、硬件帧下载前判断是否需要显示该帧。
- 对多路预览模式下的非活动流启用更激进的帧抽样策略。
- 区分活动流和非活动流，活动流保持更高预览帧率，非活动流降低预览帧率。
- 在不破坏 seek、同步和无音频流语义的前提下，将无显示价值的帧尽早释放。

该优化不应直接塞入 `MultiViewRenderer`，也不应让 UI 直接控制 decoder。应沿用现有控制链路，由应用层保存策略，领域层下发到单路管线，基础设施层在合适的位置执行。

## 风险与边界

- 当前降帧目标帧率固定为 `15fps`，后续如需细化，可以扩展为 UI 可选参数。
- 当前优化不改变解码策略，因此遇到多路高分辨率、高帧率源时仍可能出现较高 GPU/CPU 占用。
- 本报告只确认 Phase 3.14.5 预览降帧通过，不代表 9/16 路真实播放扩容已经具备条件。

## 静态检查

已执行：

```powershell
git diff --check -- src\infrastructure\videoframescheduler.h src\infrastructure\videoframescheduler.cpp src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp src\domain\MultiStreamManager.h src\domain\MultiStreamManager.cpp src\application\MediaPlayController.h src\application\MediaPlayController.cpp src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui doc\Phase3_14TODO.md
```

结果：通过。

本轮未编译，由用户人工运行验收。
