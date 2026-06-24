# Phase 3.12 MultiViewRenderer 布局与交互验收报告

## 1. 验收结论

Phase 3.12 的代码与文档任务已经完成。

本阶段完成了 `MultiViewRenderer` 作为主显示区域后的基础布局与交互补齐：

- 支持 `Single`、`Grid2x2`、`Grid3x3`、`Grid4x4` 布局语义。
- 支持新增布局的 viewport 计算。
- 主窗口布局下拉框可以选择新增布局。
- 支持双击已绑定画面进入单路聚焦，再次双击或切换布局恢复。
- 保持 `MultiViewRenderer` 只负责显示区域、点击命中和帧绘制，不接管播放管线、同步调度或 stream 生命周期。

本报告只做阶段性静态验收记录。程序运行、播放稳定性和视觉表现仍需要用户在本地环境中做人工验证。

## 2. 最终支持的布局模式

当前布局模式分为两层语义：

| 布局 | 可见 viewport 数量 | 当前真实可播放路数 | 说明 |
| --- | ---: | ---: | --- |
| `Single` | 1 | 最多 4 路中的 1 路可见 | 单画面显示 |
| `Grid2x2` | 4 | 最多 4 路 | 当前真实多路播放的主要布局 |
| `Grid3x3` | 9 | 最多 4 路 | 仅扩展显示布局，未扩展真实播放容量 |
| `Grid4x4` | 16 | 最多 4 路 | 仅扩展显示布局，未扩展真实播放容量 |

需要特别明确：`Grid3x3` 和 `Grid4x4` 表示 `MultiViewRenderer` 可以把显示区域切成 9 个或 16 个 viewport，不等于当前已经支持 9 路或 16 路真实播放。

当前真实可播放路数仍由 `MainWindow::kVideoSlotCount = 4` 和 `MaxVideoSlots()` 限制。添加新 stream 时仍通过 `FirstAvailableVideoSlotIndex()` 分配 0 到 3 的逻辑 slot。

## 3. Viewport 计算方式

`MultiViewRenderer` 当前保留两个坐标边界：

- `qtRect`：Qt 逻辑坐标，用于鼠标单击、双击命中。
- `framebufferRect`：OpenGL framebuffer 物理像素坐标，用于 `glViewport()` 和 `glScissor()`。

新增布局没有把这两个坐标系混在一起。`Grid2x2`、`Grid3x3`、`Grid4x4` 统一通过网格计算逻辑得到 Qt 坐标，再转换成 framebuffer 坐标。

该设计的目的：

- 避免高 DPI 环境下 OpenGL 绘制区域集中到左下角。
- 避免鼠标点击命中使用物理像素坐标导致偏移。
- 让后续新增布局时继续复用同一套 viewport 计算边界。

## 4. 主窗口 UI 映射

主窗口布局下拉框已经接入：

- `Single`
- `2x2`
- `3x3`
- `4x4`

选择布局时，`MainWindow` 负责把 UI 布局状态映射到 `MultiViewRenderer::Layout`。

本阶段没有修改：

- stream 添加逻辑。
- stream 删除逻辑。
- stream 生命周期管理。
- `MultiStreamManager`。
- `MediaPipeline`。
- `VideoFrameScheduler`。

布局切换日志已经区分：

- `visible slots`：当前布局可见 viewport 数量。
- `playable slots`：当前实际可播放逻辑 slot 数量。

这样可以避免把 `4x4` 误理解为已经支持 16 路播放。

## 5. 单路聚焦与恢复交互

当前交互语义：

- 单击已绑定画面：仍用于切换 active stream。
- 双击已绑定画面：进入该 stream 的单路聚焦显示。
- 再次双击同一路：退出聚焦，恢复进入聚焦前的布局。
- 切换布局：退出聚焦，进入用户选择的新布局。
- 双击空 slot：不触发聚焦。

聚焦状态由 `MainWindow` 维护，`MultiViewRenderer` 不保存聚焦状态。

具体边界：

- `MultiViewRenderer` 只做双击命中，并发出 `StreamDoubleClicked(StreamId)`。
- `MainWindow` 通过 `focusedStreamId_` 维护当前聚焦 stream。
- 聚焦时只覆盖传给 `MultiViewRenderer` 的显示布局和 slot 映射。
- 聚焦不会修改 `streamSlotIndexes_` 的真实绑定关系。
- 聚焦不会增删 stream。
- 聚焦不会暂停、停止或重建播放管线。

聚焦无音频流时，仍复用现有 active audio 策略：双击聚焦会先走 `ActivateStreamFromView()`，如果目标 stream 无音频，则 active audio stream 设置为无效值，表现与当前点击无音频流一致。

## 6. 架构边界验收

本阶段符合既定架构路线：

- `MultiViewRenderer` 没有访问 `VFrame_`。
- `MultiViewRenderer` 没有访问 `SyncManager`。
- `MultiViewRenderer` 没有做同步判断。
- `MultiViewRenderer` 没有做取帧判断。
- `MultiViewRenderer` 没有消费队列。
- `MultiViewRenderer` 没有决定 stream 绑定哪个逻辑 slot。
- `StreamId` 没有进入 `MediaPipeline`。
- `StreamId` 没有进入 `VideoFrameScheduler`。
- `VideoFrameScheduler` 仍负责单路取帧、同步判断、丢帧和调度。
- `MediaPipeline` 仍保持单路播放管线职责。
- `MainWindow` 继续承担 UI 布局选择、逻辑 slot 管理和交互转发。

## 7. 静态检查结果

本阶段各子任务均按要求做过静态检查：

- `git diff --check`
- `rg` 定向搜索关键接口、布局枚举、viewport 计算、UI 映射和聚焦交互。

本报告任务不运行程序、不编译项目。

用户本地仍建议执行以下人工验收：

1. 添加 1 路视频，切换 `Single`、`2x2`、`3x3`、`4x4`。
2. 添加 2 路视频，确认画面位置、active 高亮和音频切换正常。
3. 添加 4 路视频，确认 `2x2` 正常显示 4 路。
4. 切换到 `3x3` / `4x4`，确认只有 4 路真实画面，其余为空 slot。
5. 双击某一路进入聚焦，再次双击恢复。
6. 聚焦状态下切换布局，确认退出聚焦并应用新布局。
7. 双击空 slot，确认不触发聚焦。
8. 播放、暂停、停止、关闭程序，确认生命周期不退化。

## 8. 剩余限制

本阶段明确不解决以下问题：

- 实际播放路数仍为 4 路，没有扩展到 9/16 路。
- `Grid3x3` / `Grid4x4` 是显示布局能力，不是多路播放容量扩展。
- 非 YUV420P 视频格式仍未统一转换。
- 4K MKV 色彩异常和画面比例异常仍属于后续渲染能力扩展。
- 多路渲染性能优化未做。
- `Focus1PlusN` 布局只做设计预留，未实现。
- 全屏模式下的布局交互体验未单独优化。
- 摄像头硬件场景仍需要结合实际设备验证。
- RTSP / RTMP / HTTP 网络流未接入。

## 9. 下一阶段建议

Phase 3.12 可以收尾。后续建议不要继续在布局交互上叠加零散功能，而是进入新的阶段并重新拆 TODO。

可选方向：

1. 如果目标是“真正 9/16 路播放”，需要单独规划逻辑 slot 扩容、stream 管理压力、UI 列表交互和性能验收。
2. 如果目标是“渲染质量”，应进入视频格式转换、色彩空间、比例显示和 4K MKV 显示问题。
3. 如果目标是“工业/摄像头预览”，应结合摄像头硬件继续验证实时源、多路预览和无音频流策略。
4. 如果目标是“体验完善”，可做全屏布局交互、聚焦状态提示和快捷操作。

推荐下一步优先处理渲染输入格式与显示质量，因为它会直接影响文件视频、摄像头视频和后续网络流的统一显示效果。
