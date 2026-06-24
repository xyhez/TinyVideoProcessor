# TODO: Phase 3.11 UI 全面转向 MultiViewRenderer

## 当前进度

- 已完成：
  - Phase 3.8 已完成 `MultiViewRenderer` 单路真实帧接入。
  - Phase 3.9 已完成 `MultiViewRenderer` 多 slot 真实帧扩展。
  - Phase 3.10 已完成 `VideoFrameScheduler` 抽取。
  - `MediaPipeline` 已显式组合 `VideoFrameScheduler` 和 `IVideoFrameSink`。
  - `VideoRender` 已收口为默认单路显示 sink。
  - 文件、多路文件和摄像头路径已经可以通过 `SingleStreamFrameSinkAdapter` 把帧送入 `MultiViewRenderer`。
- 未完成：
  - 主窗口 `.ui` 中的四个旧视频容器仍存在。
  - `MainWindow` 仍使用旧容器承担 slot 容量判断、stream 绑定、布局显示隐藏、点击激活和 active 高亮职责。
  - `MultiViewRenderer` 还不是主窗口唯一视频显示区域。
- 已知风险：
  - 直接删除旧 `video_widget*` 会破坏 slot 分配、active stream 点击切换、布局刷新和默认回退路径参数。
  - 如果在本阶段修改 `VideoFrameScheduler` 或 `MediaPipeline` 调度语义，会扩大回归范围。
  - `MultiViewRenderer` 当前仍主要覆盖 YUV420P 绘制，不在本阶段处理 4K MKV 色彩、格式转换和性能优化。
- 本轮范围：
  - 迁移 UI 层旧视频容器的职责。
  - 让 `MultiViewRenderer` 成为主窗口常规播放路径的唯一显示区域。
  - 保留 `VideoRender` 作为默认 sink 回退能力，不从基础设施层删除。

## 阶段目标

把主窗口从“多个旧 QWidget 容器承载多个 `VideoRender` / 兼容 slot 逻辑”的历史路径，迁移为“一个 `MultiViewRenderer` 承载所有视频画面”的统一路径。

目标链路：

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

最终边界：

- `MainWindow` 负责 UI 交互、逻辑 slot 分配和 active stream 状态同步。
- `MediaPlayController` 继续作为 UI 到应用层的唯一入口。
- `MediaPipeline` 继续只负责单路播放管线，不感知 `StreamId`。
- `VideoFrameScheduler` 继续只负责单路取帧、同步判断、丢帧和调度。
- `MultiViewRenderer` 只负责按 `StreamId -> slot -> viewport` 绘制画面、显示 active 边框和上报点击命中。
- `VideoRender` 只保留默认 sink 回退职责，不作为主窗口常规多路显示路径。

## 架构边界

- 不把 `StreamId` 放进 `MediaPipeline`。
- 不把 `StreamId` 放进 `VideoFrameScheduler`。
- 不让 `MultiViewRenderer` 访问 `VFrame_`。
- 不让 `MultiViewRenderer` 访问 `SyncManager`。
- 不让 `MultiViewRenderer` 做同步判断、取帧判断或队列消费。
- 不删除 `VideoRender` 类。
- 不在本阶段处理非 YUV420P 格式转换。
- 不在本阶段处理渲染性能优化。
- 不在本阶段修改 active audio 策略。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase3_10_UI全面转向MultiViewRenderer复核报告.md`
- `doc/Phase3_10_VideoFrameScheduler抽取验收报告.md`
- `doc/Phase3_10TODO.md`

## 3.11.1 UI 全面转向 MultiViewRenderer TODO 拆分

**目标**：建立 Phase 3.11 独立 TODO，明确旧 UI 容器替换的顺序、边界、验收标准和不做范围。

**预计修改文件**：

- `doc/Phase3_11TODO.md`
- `doc/TODO.md`

**任务清单**：

- [x] 明确 Phase 3.11 阶段目标。
- [x] 明确旧视频容器当前仍承担的 UI 职责。
- [x] 明确不能直接删除旧容器的原因。
- [x] 拆分 3.11.2 到 3.11.8 的实施任务。
- [x] 为每个任务补充修改文件、验收标准、测试流程和 commit message。
- [x] 在主 `doc/TODO.md` 中增加 Phase 3.11 入口。

**验收标准**：

- [x] 能说明旧容器需要先迁移哪些职责。
- [x] 能说明 `MultiViewRenderer` 在本阶段负责什么、不负责什么。
- [x] 能说明为什么 `VideoRender` 不能被删除。
- [x] 后续任务可以按 TODO 顺序逐步执行。

**测试流程**：

- [x] 本任务为文档拆分，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_11TODO.md doc\TODO.md`

**Commit message**：

```text
docs: 拆分 Phase 3.11 UI 全面转向 MultiViewRenderer 任务

- 新增 Phase 3.11 独立 TODO
- 明确旧 UI 视频容器替换的阶段目标和架构边界
- 拆分逻辑 slot 管理、常规播放路径迁移、布局刷新迁移、点击激活迁移和旧容器清理任务
- 明确保留 VideoRender 作为默认 sink 回退能力
- 在主 TODO 中增加 Phase 3.11 入口
```

## 3.11.2 引入逻辑 slot 管理

**目标**：把 `MainWindow` 中的容量判断和 stream 绑定从旧 QWidget 容器迁移到纯逻辑 slot，先不删除 `.ui` 控件。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_11TODO.md`

**任务清单**：

- [ ] 定义主窗口可用逻辑 slot 数量，第一版仍保持 4 个。
- [ ] 将 `FirstAvailableVideoContainerIndex()` 迁移为基于逻辑 slot 的查找。
- [ ] 将 `streamContainerIndexes_` 的语义迁移为 `StreamId -> slotIndex`。
- [ ] 将 `BindStreamToVideoContainer()` 迁移为绑定逻辑 slot。
- [ ] 将 `ContainerIndexForStream()` / `StreamIdForVideoContainerIndex()` 迁移为 slot 语义。
- [ ] 多路文件和摄像头添加时根据逻辑 slot 判断容量。
- [ ] 暂时保留旧 `VideoContainers()`，只作为后续清理对象，不再作为容量判断依据。

**验收标准**：

- [ ] 添加多路时容量判断不依赖旧 QWidget 数量。
- [ ] stream 和 slot 的映射不依赖旧容器指针。
- [ ] `StreamId` 仍只停留在 Controller / Manager 外层和 UI 映射层，不进入 `MediaPipeline`。
- [ ] 旧容器仍未删除，降低本步回归范围。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp doc\Phase3_11TODO.md`
- [ ] 人工运行建议：添加 1 路、2 路、4 路文件，确认容量限制和 active stream 选择不退化。

**Commit message**：

```text
refactor: 将 MainWindow 多路绑定迁移为逻辑 slot

- 引入主窗口逻辑 slot 管理语义
- 将多路容量判断从旧 QWidget 容器迁移到逻辑 slot
- 将 StreamId 绑定从容器索引语义收口为 slot 索引语义
- 保留旧视频容器作为后续清理对象
- 保持 StreamId 不进入 MediaPipeline 和 VideoFrameScheduler
```

## 3.11.3 常规播放路径摆脱旧容器参数

**目标**：让播放列表切换、多路文件和摄像头添加在统一渲染路径下不再依赖旧视频容器参数。

**预计修改文件**：

- `src/ui/mainwindow.cpp`
- `doc/Phase3_11TODO.md`

**任务清单**：

- [ ] 梳理 `SwitchToPlaylistIndex()` 的 `PrimaryVideoContainer()` 依赖。
- [ ] 梳理 `AddMultiStreamFiles()` 的 `VideoContainerAt()` 依赖。
- [ ] 梳理 `AddCameraStream()` 的 `VideoContainerAt()` 依赖。
- [ ] 在存在 frame sink factory 时，向 Controller 传入空默认容器或新的兼容入口。
- [ ] 仅在没有外部 sink factory 的回退路径下使用默认 renderer container。
- [ ] 确认默认 `VideoRender` 回退路径仍可保留。

**验收标准**：

- [ ] `MultiViewRenderer` 正常路径不需要旧 `video_widget*` 作为 renderer container。
- [ ] 默认 `VideoRender` fallback 仍有明确入口。
- [ ] `MediaPipeline` 不需要感知 UI slot 或 `StreamId`。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.cpp doc\Phase3_11TODO.md`
- [ ] 人工运行建议：播放列表切换、多路文件添加、摄像头源添加均能进入统一渲染区域。

**Commit message**：

```text
refactor: 移除主窗口统一渲染路径对旧容器参数的依赖

- 调整播放列表切换和多路添加的 renderer container 传递逻辑
- 统一渲染路径下不再依赖旧 video_widget 容器
- 仅在默认 VideoRender 回退路径保留 renderer container 参数
- 保持 MediaPipeline 的单路职责和 StreamId 边界不变
```

## 3.11.4 让 MultiViewRenderer 成为主显示区域

**目标**：把 `MultiViewRenderer` 从实验/附属显示区域迁移为主窗口常规视频显示区域，旧容器只保留为临时兼容，不再承载主要显示布局。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `doc/Phase3_11TODO.md`

**任务清单**：

- [ ] 梳理当前 `multiViewRendererDock_` 的创建和生命周期。
- [ ] 决定 `MultiViewRenderer` 的主窗口承载位置。
- [ ] 将 `MultiViewRenderer` 嵌入主视频显示区域。
- [ ] 避免 `MultiViewRenderer` 同时被 dock 和主布局双重持有。
- [ ] 保留旧容器但隐藏或降低为临时兼容控件。
- [ ] 确认主窗口关闭时不引入新的 QObject 父子关系或析构顺序问题。

**验收标准**：

- [ ] 常规播放画面显示在主 `MultiViewRenderer` 区域。
- [ ] 不再需要打开实验 dock 才能看到统一渲染画面。
- [ ] 多路布局仍由 `MultiViewRenderer` 绘制。
- [ ] 析构顺序清晰，不引入重复 delete 或悬空访问风险。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui doc\Phase3_11TODO.md`
- [ ] 人工运行建议：启动、添加多路、切换布局、pause all、stop all、关闭程序。

**Commit message**：

```text
refactor: 将 MultiViewRenderer 迁移为主视频显示区域

- 将统一渲染器从实验显示区域迁移到主窗口视频区域
- 收口 multiViewRendererDock 的临时承载职责
- 保持旧视频容器仅作为过渡兼容对象
- 保持多路画面继续通过 MultiViewRenderer 绘制
- 避免改变 MediaPipeline 和 VideoFrameScheduler 边界
```

## 3.11.5 布局刷新完全交给 MultiViewRenderer

**目标**：让 `RefreshMultiViewLayout()` 只同步 `MultiViewRenderer` 的布局、slot 和 active stream，不再显示隐藏旧 QWidget 容器。

**预计修改文件**：

- `src/ui/mainwindow.cpp`
- `doc/Phase3_11TODO.md`

**任务清单**：

- [ ] 移除 `RefreshMultiViewLayout()` 中对旧容器 `setVisible()` 的依赖。
- [ ] 根据当前布局模式刷新 `MultiViewRenderer`。
- [ ] 根据逻辑 slot 映射刷新 `MultiViewRenderer::SetViewSlots()`。
- [ ] 根据 active stream 刷新 `MultiViewRenderer` active 状态。
- [ ] 确认 empty slot 的显示由 `MultiViewRenderer` 负责。

**验收标准**：

- [ ] `Single` / `Grid2x2` 布局切换只影响 `MultiViewRenderer`。
- [ ] 旧容器显示隐藏不再参与布局逻辑。
- [ ] 空 slot、active slot 和多路显示语义一致。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.cpp doc\Phase3_11TODO.md`
- [ ] 人工运行建议：单路、多路、切换 `Single` / `Grid2x2`，确认画面位置和 active 状态正确。

**Commit message**：

```text
refactor: 将多路布局刷新收口到 MultiViewRenderer

- 让 RefreshMultiViewLayout 只驱动统一渲染器布局
- 移除布局刷新对旧 QWidget 视频容器显示隐藏的依赖
- 通过逻辑 slot 映射刷新 MultiViewRenderer slot 分配
- 保持 active stream 状态同步到统一渲染器
```

## 3.11.6 点击激活和高亮完全迁移到 MultiViewRenderer

**目标**：用 `MultiViewRenderer::StreamClicked` 和统一渲染器 active 边框替代旧容器事件过滤、点击激活和 stylesheet 高亮。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_11TODO.md`

**任务清单**：

- [ ] 移除旧容器事件过滤安装逻辑。
- [ ] 移除旧容器点击到 active stream 的转换逻辑。
- [ ] 连接 `MultiViewRenderer::StreamClicked` 到主窗口 active stream 切换。
- [ ] 保持有音频流点击时同步 active audio stream。
- [ ] 保持无音频流点击时静音策略不退化。
- [ ] 移除旧容器 stylesheet active 高亮逻辑。
- [ ] 确认 active 高亮由 `MultiViewRenderer` 绘制。

**验收标准**：

- [ ] 点击 `MultiViewRenderer` 中任一路画面可切换 active stream。
- [ ] active audio stream 跟随点击策略不退化。
- [ ] 旧容器事件过滤不再参与 active stream 切换。
- [ ] active 高亮只由统一渲染器负责。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp doc\Phase3_11TODO.md`
- [ ] 人工运行建议：添加有音频和无音频视频，点击不同画面，确认进度条、音频和高亮同步。

**Commit message**：

```text
refactor: 将多路点击激活迁移到 MultiViewRenderer

- 使用 MultiViewRenderer::StreamClicked 切换 active stream
- 移除旧视频容器事件过滤和点击激活逻辑
- 将 active 高亮显示收口到统一渲染器
- 保持有音频和无音频流的 active audio 策略不退化
```

## 3.11.7 删除旧 UI 视频容器和辅助方法

**目标**：在逻辑 slot、主显示区域、布局刷新、点击激活和高亮全部迁移后，删除 `.ui` 中旧四个视频容器及相关辅助方法。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `doc/Phase3_11TODO.md`

**任务清单**：

- [ ] 删除 `.ui` 中 `video_widget`、`video_widget_1`、`video_widget_2`、`video_widget_3`。
- [ ] 删除 `VideoContainers()`。
- [ ] 删除 `PrimaryVideoContainer()`。
- [ ] 删除 `VideoContainerAt()`。
- [ ] 删除旧容器样式相关方法。
- [ ] 删除旧容器 event filter 相关方法。
- [ ] 清理不再需要的 include、字段和注释。
- [ ] 保留 `VideoRender` 类和默认 fallback 路径。

**验收标准**：

- [ ] `MainWindow` 不再引用旧 `video_widget*`。
- [ ] `.ui` 中不再保留旧四个视频容器。
- [ ] 主窗口常规显示只依赖 `MultiViewRenderer`。
- [ ] `VideoRender` 仍作为基础设施层默认 sink 存在。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui doc\Phase3_11TODO.md`
- [ ] 定向搜索：`rg -n "video_widget|VideoContainers|PrimaryVideoContainer|VideoContainerAt|InstallVideoContainerEventFilters|UpdateVideoContainerStyles" src\ui`
- [ ] 人工运行建议：启动、播放列表切换、多路添加、点击 active、pause all、stop all、关闭程序。

**Commit message**：

```text
refactor: 删除主窗口旧视频容器路径

- 删除 MainWindow 中旧 video_widget 多容器 UI
- 清理旧容器容量判断、点击激活和样式高亮辅助方法
- 让主窗口常规视频显示完全依赖 MultiViewRenderer
- 保留 VideoRender 作为默认 sink 回退实现
- 保持 MediaPipeline 和 VideoFrameScheduler 架构边界不变
```

## 3.11.8 Phase 3.11 验收报告

**目标**：收尾 UI 全面转向 `MultiViewRenderer` 阶段，记录最终链路、删除旧容器结果、保留 fallback 的原因和剩余限制。

**预计修改文件**：

- `doc/Phase3_11_UI全面转向MultiViewRenderer验收报告.md`
- `doc/Phase3_11TODO.md`

**任务清单**：

- [ ] 记录主窗口最终视频显示链路。
- [ ] 记录旧 UI 容器删除结果。
- [ ] 记录逻辑 slot 和 `MultiViewRenderer` slot 的关系。
- [ ] 记录 active stream / active audio stream 点击切换结果。
- [ ] 记录 `VideoRender` fallback 保留原因。
- [ ] 记录剩余限制：格式覆盖、性能优化、全屏目标、摄像头硬件验证。

**验收标准**：

- [ ] 能说明 Phase 3.11 是否完成。
- [ ] 能说明 `MultiViewRenderer` 是否成为主窗口唯一常规显示区域。
- [ ] 能说明旧容器职责是否全部迁移。
- [ ] 能说明下一阶段应该处理什么。

**测试流程**：

- [ ] 本任务为验收报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_11_UI全面转向MultiViewRenderer验收报告.md doc\Phase3_11TODO.md`

**Commit message**：

```text
docs: 增加 UI 全面转向 MultiViewRenderer 验收报告

- 新增 Phase 3.11 验收报告
- 记录主窗口视频显示链路全面转向 MultiViewRenderer 的结果
- 记录旧 UI 视频容器职责迁移和删除情况
- 记录 VideoRender fallback 保留原因
- 记录格式覆盖、性能优化和硬件验证等剩余限制
```

## 不做范围

- 不重构 `MediaPipeline` 的单路播放职责。
- 不修改 `VideoFrameScheduler` 的同步和取帧算法。
- 不删除 `VideoRender` 类。
- 不实现非 YUV420P 格式转换。
- 不处理 4K MKV 色彩异常。
- 不做多路渲染性能优化。
- 不新增 OpenCV 图像处理能力。
- 不新增 RTSP / RTMP 网络流。

## 风险与沟通点

- 如果 `MultiViewRenderer` 嵌入主窗口后出现析构卡住或重复释放，应优先检查 QObject 父子关系和 dock / central widget 的所有权，而不是修改播放管线。
- 如果旧容器删除后播放列表切换失败，应优先检查统一渲染路径是否仍传入外部 sink factory。
- 如果点击画面后音频策略异常，应优先检查 active stream 和 active audio stream 的同步逻辑。
- 如果某些视频黑屏或颜色异常，不应在本阶段修补；该问题属于后续格式转换和渲染能力扩展。
