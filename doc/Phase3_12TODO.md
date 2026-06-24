# TODO: Phase 3.12 MultiViewRenderer 布局与交互完善

## 当前进度

- 已完成：
  - Phase 3.8 已完成 `MultiViewRenderer` 单路真实帧接入。
  - Phase 3.9 已完成 `MultiViewRenderer` 多 slot 真实帧扩展。
  - Phase 3.10 已完成 `VideoFrameScheduler` 抽取。
  - Phase 3.11 已完成主窗口 UI 全面转向 `MultiViewRenderer`。
  - 主窗口常规视频显示已由单个 `MultiViewRenderer` 承载。
  - 旧 `video_widget*` 容器、事件过滤、点击激活和 stylesheet 高亮路径已清理。
  - Phase 3.12.2 已完成 `Grid3x3` / `Grid4x4` 布局模型定义。
  - Phase 3.12.3 已完成 `Grid3x3` / `Grid4x4` viewport 计算扩展。
- 未完成：
  - 主窗口布局下拉框尚未覆盖更多多路布局。
  - 单路聚焦/恢复仍只依赖当前 `Single` / `Grid2x2` 语义，缺少明确的聚焦交互模型。
  - Phase 3 尚未形成布局交互能力的阶段性验收报告。
- 已知风险：
  - 布局能力、全屏能力、格式转换和性能优化容易混在一起，需要本阶段严格收口。
  - `MultiViewRenderer` 的 viewport 计算属于渲染显示职责，但 stream 分配仍属于 `MainWindow` 的逻辑 slot 管理职责。
  - 如果新增布局时同步修改 `VideoFrameScheduler` 或 `MediaPipeline`，会破坏 Phase 3.11 已经建立的职责边界。
- 本轮范围：
  - 扩展 `MultiViewRenderer` 的布局表达和 viewport 计算。
  - 让主窗口 UI 能切换更多布局。
  - 明确单路聚焦/恢复交互。
  - 不处理格式转换、色彩问题、性能优化和播放管线重构。

## 阶段目标

在 Phase 3.11 已经把主窗口显示路径统一到 `MultiViewRenderer` 后，补齐它作为主显示区域的基础布局和交互能力。

目标链路保持不变：

```text
MainWindow
    -> 逻辑 slot 管理
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline
                -> VideoFrameScheduler
                    -> IVideoFrameSink
                        -> SingleStreamFrameSinkAdapter
                            -> MultiViewRenderer
```

本阶段只扩展：

- `MainWindow` 的布局选择和交互入口。
- `MultiViewRenderer` 的布局枚举、viewport 计算、点击命中和 active 高亮表现。

本阶段不改变：

- `MediaPipeline` 单路播放职责。
- `VideoFrameScheduler` 取帧、同步判断、丢帧和调度职责。
- `MultiStreamManager` 的 stream 生命周期管理职责。
- `IVideoFrameSink` 的帧所有权语义。

## 架构边界

- 不把 `StreamId` 放进 `MediaPipeline`。
- 不把 `StreamId` 放进 `VideoFrameScheduler`。
- 不让 `MultiViewRenderer` 访问 `VFrame_`。
- 不让 `MultiViewRenderer` 访问 `SyncManager`。
- 不让 `MultiViewRenderer` 做同步判断、取帧判断或队列消费。
- 不让 `MultiViewRenderer` 决定 stream 应该绑定哪个 slot。
- 不在本阶段实现非 YUV420P 格式转换。
- 不在本阶段处理 4K MKV 色彩和画面比例异常。
- 不在本阶段做多路渲染性能优化。
- 不在本阶段接入 RTSP / RTMP / HTTP 网络流。

## 参考文档

- `doc/架构指导.md`
- `doc/TODO.md`
- `doc/Phase3_11TODO.md`
- `doc/Phase3_11_UI全面转向MultiViewRenderer验收报告.md`
- `doc/Phase3_9_MultiViewRenderer多slot真实帧扩展验收报告.md`
- `doc/Phase3_6_MultiViewRenderer空壳实现报告.md`

## 3.12.1 Phase 3.12 TODO 拆分

**目标**：建立 Phase 3.12 独立 TODO，明确布局与交互完善的顺序、边界、验收标准和不做范围。

**预计修改文件**：

- `doc/Phase3_12TODO.md`
- `doc/TODO.md`

**任务清单**：

- [x] 明确 Phase 3.12 阶段目标。
- [x] 明确布局能力和播放管线的边界。
- [x] 明确本阶段不处理格式转换、性能优化和同步调度。
- [x] 拆分布局模型、viewport 计算、UI 映射、聚焦交互和验收报告任务。
- [x] 为每个任务补充修改文件、验收标准、测试流程和 commit message。
- [x] 在主 `doc/TODO.md` 中增加 Phase 3.12 入口。

**验收标准**：

- [x] 能说明 Phase 3.12 做什么、不做什么。
- [x] 能说明为什么本阶段不碰 `MediaPipeline` 和 `VideoFrameScheduler`。
- [x] 后续任务可以按 TODO 顺序逐步执行。

**测试流程**：

- [x] 本任务为文档拆分，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_12TODO.md doc\TODO.md`

**Commit message**：

```text
docs: 拆分 Phase 3.12 MultiViewRenderer 布局与交互任务

- 新增 Phase 3.12 独立 TODO
- 明确 MultiViewRenderer 布局与交互完善的阶段目标
- 明确本阶段不处理播放管线、同步调度、格式转换和性能优化
- 拆分布局模型、viewport 计算、UI 映射、聚焦交互和验收报告任务
- 在主 TODO 中增加 Phase 3.12 入口
```

## 3.12.2 布局模型扩展设计

**目标**：先扩展布局枚举和语义，不急于实现所有复杂布局。第一步推荐新增 `Grid3x3` 和 `Grid4x4` 的模型定义，`Focus1PlusN` 只做设计预留。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/infrastructure/multiviewrenderer.h`
- `doc/Phase3_12TODO.md`

**任务清单**：

- [x] 梳理当前 `MultiViewLayout` 与 `MultiViewRenderer::Layout` 的映射。
- [x] 新增 `Grid3x3` 布局语义。
- [x] 新增 `Grid4x4` 布局语义。
- [x] 明确 `Grid3x3` 最大可见 slot 数量为 9。
- [x] 明确 `Grid4x4` 最大可见 slot 数量为 16。
- [x] 明确当前 stream 容量是否仍先保持 4 路，还是同步扩展逻辑 slot 容量。
- [x] 对 `Focus1PlusN` 做设计预留，但不在本步实现。
- [x] 不修改播放管线。

**验收标准**：

- [x] 代码中能表达 `Grid3x3` / `Grid4x4` 布局。
- [x] 布局枚举语义清晰，不和 active stream 策略混淆。
- [x] 如果本步决定暂不扩展实际 stream 容量，文档中必须明确原因。

**本步结论**：

- 当前仅扩展布局模型语义，实际 stream 容量仍保持 4 路。
- `Grid3x3` / `Grid4x4` 表示显示布局可见区域能力，不等同于本步已经支持 9/16 路真实播放。
- 9/16 路真实播放需要另行评估逻辑 slot 容量、UI 列表交互、多路生命周期压力和渲染性能，不在 3.12.2 中一起扩展。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp src\infrastructure\multiviewrenderer.h doc\Phase3_12TODO.md`
- [x] 定向搜索：`rg -n "Grid3x3|Grid4x4|Focus1PlusN|MaxVisibleSlots|MultiViewLayout|Layout" src\ui src\infrastructure doc\Phase3_12TODO.md`
- [ ] 人工运行建议：启动程序，确认默认布局不退化。

**Commit message**：

```text
feat: 扩展 MultiViewRenderer 布局模型

- 新增 Grid3x3 和 Grid4x4 布局语义
- 梳理 MainWindow 布局枚举与 MultiViewRenderer 布局枚举的映射
- 明确新增布局的最大可见 slot 数量
- 保持 MediaPipeline 和 VideoFrameScheduler 边界不变
- 为后续 Focus1PlusN 聚焦布局预留设计空间
```

## 3.12.3 Viewport 计算扩展

**目标**：让 `MultiViewRenderer` 能根据新增布局计算正确 viewport，保持 Qt 逻辑坐标和 OpenGL framebuffer 坐标分离。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `doc/Phase3_12TODO.md`

**任务清单**：

- [x] 将 `Grid2x2` 的网格计算抽成通用网格 viewport 计算方法。
- [x] 支持 `Grid3x3` viewport 计算。
- [x] 支持 `Grid4x4` viewport 计算。
- [x] 保持 `qtRect` 用于鼠标命中。
- [x] 保持 `framebufferRect` 用于 `glViewport()` / `glScissor()`。
- [x] 确认高 DPI 下新增布局不再出现左下角集中问题。
- [x] 空 slot 仍由 `MultiViewRenderer` 绘制背景。

**验收标准**：

- [x] `Grid2x2` 行为不退化。
- [x] `Grid3x3` 能计算 9 个 viewport。
- [x] `Grid4x4` 能计算 16 个 viewport。
- [x] 鼠标点击命中仍使用 Qt 逻辑坐标。
- [x] OpenGL 绘制仍使用 framebuffer 物理像素。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_12TODO.md`
- [x] 定向搜索：`rg -n "QtRectForSlot|ToFramebufferRect|FramebufferSize|MaxVisibleSlots|Grid3x3|Grid4x4" src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_12TODO.md`
- [ ] 人工运行建议：切换 `Single` / `Grid2x2` / `Grid3x3` / `Grid4x4`，确认画面区域位置正确。

**Commit message**：

```text
feat: 扩展 MultiViewRenderer viewport 计算

- 将网格布局 viewport 计算收口为通用逻辑
- 支持 Grid3x3 和 Grid4x4 的 viewport 计算
- 保持 Qt 逻辑坐标用于点击命中
- 保持 OpenGL framebuffer 坐标用于 viewport 和 scissor
- 保持 Grid2x2 和 active 高亮行为不退化
```

## 3.12.4 主窗口布局 UI 映射

**目标**：让主窗口布局下拉框可以选择新增布局，并将布局状态同步到 `MultiViewRenderer`。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_12TODO.md`

**任务清单**：

- [ ] 在布局下拉框中增加 `3x3` 选项。
- [ ] 在布局下拉框中增加 `4x4` 选项。
- [ ] 更新 `InitializeMultiViewLayout()` 的默认选项。
- [ ] 更新 `SetMultiViewLayout()` 的布局同步逻辑。
- [ ] 更新 `RefreshMultiViewRenderer()` 的布局映射逻辑。
- [ ] 如果当前逻辑 slot 仍保持 4 路，UI 文案或日志应避免暗示已经支持 9/16 路播放。
- [ ] 不修改 stream 添加、删除和播放生命周期逻辑，除非 3.12.2 明确决定扩容。

**验收标准**：

- [ ] 布局下拉框能选择新增布局。
- [ ] 选择新增布局后 `MultiViewRenderer` 显示区域发生对应变化。
- [ ] 当前 active stream 和 active 高亮不退化。
- [ ] 不影响播放、暂停、停止和关闭。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp doc\Phase3_12TODO.md`
- [ ] 定向搜索：`rg -n "InitializeMultiViewLayout|SetMultiViewLayout|RefreshMultiViewRenderer|Grid3x3|Grid4x4" src\ui doc\Phase3_12TODO.md`
- [ ] 人工运行建议：添加 1 路、2 路、4 路文件后切换所有布局。

**Commit message**：

```text
feat: 接入主窗口多路布局选择

- 在主窗口布局选择中增加 Grid3x3 和 Grid4x4
- 将新增布局状态同步到 MultiViewRenderer
- 保持 active stream 和 active 高亮同步逻辑不变
- 不修改播放管线和视频帧调度边界
```

## 3.12.5 单路聚焦与恢复交互

**目标**：设计并实现明确的单路聚焦/恢复交互，让用户可以临时放大某一路画面，再恢复到原布局。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_12TODO.md`

**任务清单**：

- [ ] 明确触发方式：优先考虑双击已绑定画面。
- [ ] 明确恢复方式：再次双击或切换布局恢复。
- [ ] 明确聚焦状态归属：由 `MainWindow` 维护还是由 `MultiViewRenderer` 维护。
- [ ] 保持 click 仍用于 active stream 切换。
- [ ] double click 只改变显示布局，不改变 stream 生命周期。
- [ ] 聚焦无音频流时保持现有 active audio 策略。
- [ ] 聚焦状态下仍能绘制 active 高亮。
- [ ] 退出聚焦后恢复进入聚焦前的布局。

**验收标准**：

- [ ] 双击某一路可以放大该路。
- [ ] 再次双击或切换布局可以恢复。
- [ ] active stream 和 active audio stream 策略不退化。
- [ ] 聚焦/恢复不影响播放生命周期。
- [ ] 空 slot 双击不触发聚焦。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\ui\mainwindow.h src\ui\mainwindow.cpp doc\Phase3_12TODO.md`
- [ ] 定向搜索：`rg -n "mouseDoubleClickEvent|StreamDoubleClicked|Focus|focused|ActivateStreamFromView|SetMultiViewLayout" src\infrastructure src\ui doc\Phase3_12TODO.md`
- [ ] 人工运行建议：2 路和 4 路播放时分别双击有音频/无音频画面，确认画面、声音和高亮符合预期。

**Commit message**：

```text
feat: 增加 MultiViewRenderer 单路聚焦交互

- 为统一渲染区域增加双击聚焦和恢复语义
- 保持单击仍用于 active stream 切换
- 聚焦状态只影响显示布局，不改变播放管线生命周期
- 保持 active audio stream 策略不退化
- 支持从聚焦状态恢复到进入前布局
```

## 3.12.6 Phase 3.12 验收报告

**目标**：收尾 MultiViewRenderer 布局与交互完善阶段，记录新增布局、聚焦交互、验证结果和剩余限制。

**预计修改文件**：

- `doc/Phase3_12_MultiViewRenderer布局与交互验收报告.md`
- `doc/Phase3_12TODO.md`

**任务清单**：

- [ ] 记录最终支持的布局模式。
- [ ] 记录逻辑 slot 数量和实际可播放路数的关系。
- [ ] 记录 viewport 计算方式和高 DPI 坐标边界。
- [ ] 记录主窗口布局 UI 映射结果。
- [ ] 记录单路聚焦/恢复交互结果。
- [ ] 记录剩余限制：格式覆盖、性能优化、更多布局策略、全屏体验、摄像头硬件验证。

**验收标准**：

- [ ] 能说明 Phase 3.12 是否完成。
- [ ] 能说明新增布局是否只影响显示，不影响播放管线。
- [ ] 能说明聚焦交互是否不改变 stream 生命周期。
- [ ] 能说明下一阶段应该处理什么。

**测试流程**：

- [ ] 本任务为验收报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_12_MultiViewRenderer布局与交互验收报告.md doc\Phase3_12TODO.md`

**Commit message**：

```text
docs: 增加 MultiViewRenderer 布局与交互验收报告

- 新增 Phase 3.12 验收报告
- 记录 MultiViewRenderer 新增布局模式和 viewport 计算结果
- 记录主窗口布局 UI 映射和单路聚焦交互
- 说明布局交互不改变播放管线和同步调度边界
- 记录格式覆盖、性能优化和全屏体验等剩余限制
```

## 不做范围

- 不修改 `MediaPipeline` 单路播放职责。
- 不修改 `VideoFrameScheduler` 取帧、同步判断、丢帧和调度算法。
- 不让 `MultiViewRenderer` 直接访问 `VFrame_` 或 `SyncManager`。
- 不实现非 YUV420P 格式转换。
- 不处理 4K MKV 色彩异常和画面比例异常。
- 不做多路渲染性能优化。
- 不新增 RTSP / RTMP / HTTP 网络流能力。
- 不新增 OpenCV 图像处理能力。
- 不删除 `VideoRender` fallback。

## 风险与沟通点

- 如果要把实际播放路数从 4 路扩展到 9/16 路，需要同步评估 `MainWindow` 逻辑 slot、`MultiStreamManager` 生命周期压力、UI 列表可用性和机器性能，不应只改布局枚举。
- 如果新增布局后出现画面位置异常，应优先检查 `qtRect -> framebufferRect` 转换，而不是修改播放管线。
- 如果聚焦交互导致音频不符合预期，应优先检查 active stream 和 active audio stream 同步策略，而不是修改 `VideoFrameScheduler`。
- 如果某些视频黑屏、颜色异常或画面比例异常，不应在本阶段修补；该问题属于后续格式转换和渲染能力扩展。
