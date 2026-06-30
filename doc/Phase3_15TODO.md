# TODO: Phase 3.15 9/16 路真实播放扩容

## 当前进度

- 已完成：
  - `MultiViewRenderer` 已支持 `Grid3x3` 和 `Grid4x4` 可见 viewport。
  - 当前布局已经能显示 9/16 个逻辑区域。
  - 当前真实播放容量仍受逻辑 slot 和播放管线资源策略限制。
  - Phase 3.15.1 已完成扩容准入复核：9 路作为正式推进目标，16 路作为实验/受限支持目标。
  - Phase 3.15.2 已完成 `MainWindow` 逻辑 slot 容量扩展：全局最大 16，当前布局控制可分配容量。
  - Phase 3.15.3 已完成多路 stream 列表可用性完善：状态前置显示、双击切换、删除后兜底选中、无音频流按钮保护。
  - Phase 3.15.4 已完成自然播放结束后的单路管线生命周期保护，避免 finished 管线继续进入 suspend-wait。
  - Phase 3.15.5 已补充 4x4/16 路实验模式提示，明确 16 路真实播放受机器性能和视频规格影响。
  - Phase 3.15.5 已通过人工验证：高负载下程序不会崩溃，16 路按实验支持收口。
- 前置要求：
  - Phase 3.13 应先完成显示正确性修复。
  - Phase 3.14 应先完成多路性能基线和低风险优化。
- 未完成：
  - Phase 3.15 验收报告。
- 本阶段范围：
  - 将 3x3 / 4x4 从“布局可见能力”推进到“真实播放容量”。

## 阶段目标

让系统可以真实添加、启动、显示、暂停、停止和移除 9 路或 16 路媒体源，并保持基本可用的生命周期和交互稳定性。

本阶段核心不是只把常量从 4 改成 16，而是验证完整链路：

```text
MainWindow 逻辑 slot
    -> MediaPlayController 多路入口
        -> MultiStreamManager stream 生命周期
            -> MediaPipeline 单路播放
                -> VideoFrameScheduler 调度
                    -> SingleStreamFrameSinkAdapter
                        -> MultiViewRenderer 对应 slot 绘制
```

## 架构边界

- 不把 `StreamId` 放进 `MediaPipeline`。
- 不把 `StreamId` 放进 `VideoFrameScheduler`。
- 不让 `MultiViewRenderer` 决定 stream 与 slot 的绑定。
- 不让 `MultiViewRenderer` 管理播放管线生命周期。
- 不通过跳过 `MultiStreamManager` 来直接创建多路管线。
- 不为了扩容破坏 active stream / active audio stream 语义。
- 不承诺所有机器都能稳定 16 路 4K 播放。

## 参考文档

- `doc/架构指导.md`
- `doc/TODO.md`
- `doc/Phase3_12TODO.md`
- `doc/Phase3_12_MultiViewRenderer布局与交互验收报告.md`
- `doc/Phase3_13TODO.md`
- `doc/Phase3_14TODO.md`
- `doc/Phase3_14_多路性能优化验收报告.md`
- `doc/Phase3_15_扩容准入复核报告.md`

## 3.15.1 扩容准入复核

**目标**：确认 9/16 路扩容是否具备前置条件。

**预计修改文件**：

- `doc/Phase3_15_扩容准入复核报告.md`
- `doc/Phase3_15TODO.md`

**任务清单**：

- [x] 确认 Phase 3.13 显示正确性问题已收口。
- [x] 确认 Phase 3.14 已形成 4 路性能基线。
- [x] 确认 9 路扩容目标视频规格。
- [x] 确认 16 路扩容目标视频规格。
- [x] 明确默认扩容顺序：先 9 路，再 16 路。
- [x] 明确低配机器的降级策略。
- [x] 明确是否允许限制单路分辨率或帧率。

**验收标准**：

- [x] 能说明是否可以进入 9 路真实播放扩容。
- [x] 能说明 16 路是否作为同阶段目标还是后续目标。
- [x] 能说明扩容验收视频规格。

**测试流程**：

- [x] 本任务为复核报告，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_15_扩容准入复核报告.md doc\Phase3_15TODO.md`
- [x] 复核报告：`doc/Phase3_15_扩容准入复核报告.md`

**Commit message**：

```text
docs: 复核 9/16 路播放扩容准入条件

- 新增 9/16 路真实播放扩容准入复核报告
- 确认显示正确性和多路性能优化前置条件
- 明确 9 路和 16 路扩容的视频规格与降级策略
- 为真实播放容量扩展建立验收边界
```

## 3.15.2 逻辑 slot 容量扩展

**目标**：把主窗口逻辑 slot 管理从 4 路扩展到布局所需容量。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_15TODO.md`

**任务清单**：

- [x] 复核当前 `kVideoSlotCount` 或等价容量限制。
- [x] 将逻辑 slot 容量与当前布局最大可见 slot 区分。
- [x] `Grid3x3` 下允许分配 9 个逻辑 slot。
- [x] `Grid4x4` 下允许分配 16 个逻辑 slot。
- [x] `Single` 下不销毁其他 stream，只改变可见策略。
- [x] 添加 stream 时根据当前布局和容量策略决定是否允许。
- [x] 移除 stream 后释放对应逻辑 slot。
- [x] 布局切换后重新同步 `MultiViewRenderer::SetViewSlots()`。

**验收标准**：

- [x] 选择 3x3 后可以添加超过 4 路。
- [x] 选择 4x4 后可以添加超过 9 路。
- [x] slot 分配不重复、不泄漏。
- [x] 移除一路后 slot 可复用。
- [x] `MultiViewRenderer` 仍不决定 slot 绑定。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp doc\Phase3_15TODO.md`
- [x] 定向搜索：`rg -n "kMaxVideoSlotCount|MaxVisibleStreams|CurrentVideoSlotCapacity|Release|viewSlots|Grid3x3|Grid4x4" src\ui doc\Phase3_15TODO.md`
- [ ] 人工运行建议：在 3x3 下添加 5-9 路，在 4x4 下添加 10-16 路。

**Commit message**：

```text
feat: 扩展主窗口逻辑 slot 容量

- 将多路逻辑 slot 容量从 4 路扩展到 3x3/4x4 布局需求
- 保持 stream 与 slot 绑定仍由 MainWindow 管理
- 布局切换时同步 MultiViewRenderer 的 view slot 映射
- 支持移除 stream 后复用逻辑 slot
```

## 3.15.3 多路添加与 UI 可用性完善

**目标**：让大量 stream 的添加、选择、移除和状态展示仍可用。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- 可选：`src/ui/mainwindow.ui`
- `doc/Phase3_15TODO.md`

**任务清单**：

- [x] 复核文件列表添加多路的交互方式。
- [x] 复核 Streams 列表大量条目显示。
- [x] 确保 active stream 切换在 9/16 路下仍准确。
- [x] 确保 active audio stream 在大量 stream 下仍最多一路有声。
- [x] 无音频流加入后不破坏音频策略。
- [x] 删除 active stream 后选择合理兜底 stream。
- [x] 删除 active audio stream 后选择合理兜底音频流或静音。

**验收标准**：

- [x] 9 路 stream 列表可读可选。
- [x] 16 路 stream 列表不影响基本操作。
- [x] 点击/双击 viewport 后 active stream 正确。
- [x] 音频策略仍然最多一路输出。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\ui\mainwindow.cpp doc\Phase3_15TODO.md`
- [x] 定向搜索：`rg -n "Streams|ActiveStream|ActiveAudio|RemoveStream|AddStream|StreamClicked|StreamDoubleClicked" src\ui doc\Phase3_15TODO.md`
- [ ] 人工运行建议：混合添加有音频和无音频视频，执行点击、双击、移除、停止、关闭。

**Commit message**：

```text
feat: 完善 9/16 路 stream UI 交互

- 支持大量 stream 的列表展示、选择和移除
- 保持 active stream 与 viewport 点击交互一致
- 保持 active audio stream 最多一路有声策略
- 处理删除 active stream 和 active audio stream 后的兜底选择
```

## 3.15.4 9 路真实播放验证与修正

**目标**：先完成 3x3 真实播放容量，不直接跳到 16 路。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `doc/Phase3_15TODO.md`

**任务清单**：

- [x] 添加 9 路文件源。
- [x] 启动全部 9 路。
- [x] 暂停全部 9 路。
- [x] 停止全部 9 路。
- [x] 移除其中任意一路。
- [x] 关闭程序。
- [x] 验证无音频流混入不崩溃。
- [x] 验证 seek active stream 不影响其他 stream 生命周期。
- [x] 根据静态链路检查修正自然播放结束后的生命周期边界问题。

**验收标准**：

- [x] 9 路可以真实创建并显示。
- [x] 9 路 start / pause / stop / shutdown 不阻塞。
- [x] 移除任意一路不会访问已释放资源。
- [x] 最多一路有声音。
- [x] 关闭程序不挂起。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\ui\mainwindow.cpp src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp doc\Phase3_15TODO.md`
- [x] 定向搜索：`rg -n "StartAll|PauseAll|StopAll|ShutdownAll|RemoveStream|DetachStream|InvalidStreamId|playbackFinished_" src\ui src\application src\domain doc\Phase3_15TODO.md`
- [x] 人工运行建议：9 路普通文件、普通文件加无音频文件、播放中关闭、暂停后关闭。

**Commit message**：

```text
feat: 支持 9 路真实播放验证

- 扩展多路播放链路以支持 3x3 下 9 路真实 stream
- 验证 9 路 start、pause、stop、remove 和 shutdown 生命周期
- 保持 active stream 和 active audio stream 策略稳定
- 处理无音频流混入 9 路播放场景
```

## 3.15.5 16 路真实播放验证与降级策略

**目标**：在 9 路稳定后，评估并推进 4x4 的 16 路真实播放。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `doc/Phase3_15TODO.md`

**任务清单**：

- [x] 添加 16 路文件源。
- [x] 启动全部 16 路。
- [x] 验证 16 路布局显示。
- [x] 验证暂停、停止、移除和关闭。
- [x] 记录性能瓶颈和机器限制。
- [x] 必要时增加容量保护或提示。
- [x] 高负载下暂不限制高分辨率多路同时播放，先采用实验模式提示。
- [x] 明确 16 路是完全支持还是实验支持。

**验收标准**：

- [x] 16 路至少可以完成创建、显示、停止和关闭。
- [x] 如果不能稳定播放，系统有明确降级或提示，不崩溃。
- [x] 4 路和 9 路能力不被 16 路改动破坏。
- [x] 音频策略仍然最多一路输出。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp doc\Phase3_15TODO.md`
- [x] 定向搜索：`rg -n "Grid4x4|16|capacity|MaxVisible|AddStream|StartAll|ShutdownAll|ShowGrid4x4CapacityNoticeIfNeeded" src\ui src\application src\domain doc\Phase3_15TODO.md`
- [x] 人工运行建议：16 路低分辨率文件、混合分辨率文件、播放中切布局、关闭程序。

**Commit message**：

```text
feat: 扩展 16 路真实播放能力

- 在 4x4 布局下验证 16 路真实 stream 创建和显示
- 覆盖 16 路 start、pause、stop、remove 和 shutdown 生命周期
- 根据性能瓶颈增加容量保护或降级提示
- 保持 4 路和 9 路播放能力不退化
```

## 3.15.6 Phase 3.15 验收报告

**目标**：收口真实播放容量扩展阶段，明确 9/16 路最终支持结论。

**预计修改文件**：

- `doc/Phase3_15_9_16路真实播放扩容验收报告.md`
- `doc/Phase3_15TODO.md`

**任务清单**：

- [ ] 记录最终支持的真实播放路数。
- [ ] 记录 9 路和 16 路验证结果。
- [ ] 记录测试视频规格。
- [ ] 记录性能瓶颈。
- [ ] 记录降级策略。
- [ ] 记录对简历项目的可展示成果。
- [ ] 给出 Phase 3 后续是否进入网络流或图像处理的建议。

**验收标准**：

- [ ] 能说明 9 路是否正式支持。
- [ ] 能说明 16 路是否正式支持或实验支持。
- [ ] 能说明机器性能和视频规格限制。
- [ ] 能说明 Phase 3 是否达到多路预览主线目标。

**测试流程**：

- [ ] 本任务为验收报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_15_9_16路真实播放扩容验收报告.md doc\Phase3_15TODO.md`

**Commit message**：

```text
docs: 增加 9/16 路真实播放扩容验收报告

- 记录 Phase 3.15 真实播放容量扩展结果
- 汇总 9 路和 16 路播放、控制、移除和关闭验证
- 说明性能瓶颈、视频规格限制和降级策略
- 明确 Phase 3 多路预览主线的完成状态
```

## 不做范围

- 不修复非 `YUV420P` 或 4K MKV 显示正确性问题。
- 不做复杂 GPU 异步上传。
- 不接入网络流。
- 不接入工业相机 SDK。
- 不实现图像处理算法。
- 不改变 `MediaPipeline` 单路职责。
- 不删除 `VideoRender` fallback。

## 风险与沟通点

- 9/16 路真实播放很可能受机器 CPU/GPU、视频分辨率、编码格式和硬解能力影响。
- 16 路不应默认承诺支持 1080p/4K 全实时播放；需要按视频规格定义验收标准。
- 如果扩容后出现关闭卡住，优先检查 stream 生命周期和 queued UI 更新清理，不应修改渲染器去管理管线。
