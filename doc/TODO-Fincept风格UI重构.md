# TODO-Fincept 风格 UI 重构

## 1. 当前进度

- 开发分支：`dev_ui_refactor`
- 基准分支：`dev`
- 架构文档：`doc/UI重构架构指导.md`
- 已完成：Phase 0～Phase 6 已完成静态验收，包括关联对话框视觉统一、中文文案、空状态、禁用原因和关键操作提示收口。
- 当前阶段：Phase 7 最终验收正在实施。
- 本轮范围：执行全量静态范围审计，复用现有 Qt 6.5.3 + MSVC Debug 环境编译运行，形成验收报告和真实截图；不修改构建配置、依赖或媒体语义，并明确排除 Qt Creator 自动更新的 `CMakeLists.txt.user`。
- 未完成：Phase 7 静态与动态验收、报告、截图、提交和推送。
- 默认验证方式：静态检查；不编译、不运行，除非用户明确要求。

## 2. 已知风险

- `MainWindow` 已包含较多状态同步逻辑，布局重构不能继续向其中堆叠主题字符串。
- `pushButton_play` 和 `pushButton_pause` 使用 Qt 自动连接槽，重排时必须保留对象名。
- 其他控件虽然多数使用显式 `connect()`，仍被大量代码通过 `ui->objectName` 访问，第一里程碑统一保留对象名。
- 第一阶段应用全局 QSS 可能暴露个别 Qt 控件的平台差异，但不应改变业务行为。
- Phase 1 不强制切换 Fusion 风格或修改系统字号，避免扩大控件尺寸和平台行为的验收范围。
- `MultiViewRenderer` 使用原生 OpenGL 绘制，普通 QSS 只能影响其外围容器，画面内浮层必须放到独立 Phase。
- 当前 CMake 使用递归源码收集，新增 `src/ui/theme/*.h/.cpp` 无需修改 `CMakeLists.txt`。
- 设计稿中的 FPS、分辨率、编码和丢包率必须来自真实状态；Phase 5 前禁止写死。
- 现有调度器丢帧只表示同步追帧和预览限帧，不是网络丢包；Phase 5 必须使用“调度丢帧”文案。
- `Fileinfo::FrameRate` 默认值可能保留为 30 FPS，不能作为真实运行 FPS；Phase 5 使用累计提交帧数按采样窗口计算。

---

## 3. 控件迁移基线

后续重排 `mainwindow.ui` 时按下表迁移。除非 TODO 先更新并说明原因，否则对象名保持不变。

| 当前对象名 | 当前职责 | 目标区域 | 连接约束 |
|---|---|---|---|
| `centralwidget` | 主窗口中央容器 | 工作台根容器 | `MainWindow` 鼠标跟踪和事件过滤依赖 |
| `listWidget_files` | 文件播放列表 | 左侧“媒体库”区域 | 多处显式连接和刷新逻辑依赖 |
| `pushButton_prev` | 上一项 | 底部播放区 | 显式连接 |
| `pushButton_next` | 下一项 | 底部播放区 | 显式连接 |
| `comboBox_mode` | 播放模式 | 左侧媒体库辅助区或底部次级区 | 显式连接和状态同步 |
| `listWidget_streams` | 多路视频源列表 | 左侧“视频源”区域 | 显式连接、选择和双击逻辑依赖 |
| `pushButton_addStream` | 添加多路文件源 | 顶部命令区 | 显式连接 |
| `pushButton_addCamera` | 添加摄像头 | 顶部命令区 | 显式连接 |
| `pushButton_addNetwork` | 添加 RTSP | 顶部命令区 | 显式连接 |
| `pushButton_removeStream` | 移除选中流 | 左侧视频源操作区 | 显式连接和可用状态依赖 |
| `pushButton_activeStream` | 设置活动视频 | 左侧视频源操作区 | 显式连接和可用状态依赖 |
| `pushButton_activeAudio` | 设置活动音频 | 左侧视频源操作区 | 显式连接、ToolTip 和可用状态依赖 |
| `pushButton_startAll` | 全部启动 | 顶部命令区 | 显式连接 |
| `pushButton_pauseAll` | 全部暂停 | 顶部命令区 | 显式连接 |
| `pushButton_stopAll` | 全部停止 | 顶部命令区 | 显式连接 |
| `widget_videoArea` | 视频显示容器 | 中央视频区 | 初始化、鼠标状态和全屏逻辑依赖 |
| `gridLayout_videoArea` | 挂载统一渲染器 | 中央视频区 | `InitializeMultiViewRenderer()` 直接依赖 |
| `label_current` | 当前播放时间 | 底部播放区 | 位置变化和 Seek 逻辑依赖 |
| `slider_progress` | 播放进度与 Seek | 底部播放区 | 多个拖动信号和状态更新依赖 |
| `label_duration` | 总时长或 LIVE | 底部播放区 | 时长状态逻辑依赖 |
| `widget_func` | 当前功能控制容器 | 后续拆分为右侧和底部区域 | 鼠标跟踪和事件过滤依赖，拆分时需同步处理 |
| `pushButton_play` | 播放 | 底部播放区 | `on_pushButton_play_clicked()` 自动连接，必须保留对象名 |
| `pushButton_pause` | 暂停 | 底部播放区 | `on_pushButton_pause_clicked()` 自动连接，必须保留对象名 |
| `slider_volume` | 音量 | 底部播放区 | 显式连接和状态同步 |
| `pushButton_mute` | 静音 | 底部播放区 | 显式连接和文案同步 |
| `comboBox_speed` | 播放速度 | 底部播放区 | 显式连接和快捷键状态同步 |
| `comboBox_layout` | 多画面布局 | 顶部命令区 | 显式连接和布局同步 |
| `radioButton_originalPreviewFrameRate` | 原始帧率预览 | 右侧“基本信息/预览”区 | 显式连接和状态同步 |
| `radioButton_reducePreviewFrameRate` | 降帧预览 | 右侧“基本信息/预览”区 | 显式连接和状态同步 |
| `checkBox_imageProcessingEnabled` | 图像处理开关 | 右侧“图像处理”区 | 显式连接和状态同步 |
| `comboBox_imageProcessingMode` | 图像处理模式 | 右侧“图像处理”区 | 显式连接和状态同步 |
| `label_imageProcessingThreshold` | 阈值标签 | 右侧“图像处理”区 | 可见和可用状态同步 |
| `spinBox_imageProcessingThreshold` | 图像处理阈值 | 右侧“图像处理”区 | 显式连接和状态同步 |
| `comboBox_visionPipeline` | 视觉分析管线 | 右侧“视觉分析”区 | 显式连接和状态同步 |
| `checkBox_visionOverlay` | 识别叠加 | 右侧“视觉分析”区 | 显式连接和状态同步 |
| `pushButton_fullscreen` | 全屏切换 | 底部播放区或顶部窗口操作区 | 显式连接、文案和快捷键同步 |

补充约束：

- `radioButton_originalPreviewFrameRate` 与 `radioButton_reducePreviewFrameRate` 当前依赖同一父控件下的 `autoExclusive` 行为；迁入右侧面板时必须保持同一直接父对象，或显式使用 `QButtonGroup`。
- `widget_videoArea`、`gridLayout_videoArea` 和运行时创建的 `multiViewRenderer` 共同构成唯一渲染挂载链路，应整体迁移，不能用新的空容器替换。
- `pushButton_stopAll` 只表示“全部停止”，当前不存在单路停止按钮；Phase 2 不得将其复用为底部单路停止。
- `pushButton_addStream` 添加多路文件源，`OpenFile()`/`Ctrl+O` 操作播放列表，两条文件入口语义不同，重排后仍须明确区分。
- 新增顶部和右侧容器后，必须重新检查全屏鼠标活动事件过滤范围。
- `splitter_workspace` 的 0/1/2 索引分别对应左侧资源区、中央视频区和右侧检查器；后续调整子项顺序时必须同步检查伸缩因子代码。
- 当前全屏行为是整个 `MainWindow` 全屏，布局重构不改变这一既有语义；仅显示视频区的影院模式属于额外功能。
- Controller 仅有全局 `PlaybackState`，没有逐流播放状态 getter 或信号；Phase 3 不得把全局状态冒充为每一路状态。
- `StreamErrorOccurred` 没有恢复通知；列表只标记“最近错误”，并在流移除时清理，不能声称当前仍处于错误状态。

---

## 4. Phase 0：方案与基线

### [x] Phase 0.1：创建 UI 重构架构指导

- 目标：明确愿景、设计原则、功能边界、模块职责、开发 Phase、工期和风险。
- 修改文件：
  - `doc/UI重构架构指导.md`
- 验收标准：
  - 包含项目愿景、设计原则、整体架构、Phase、工期、最终效果和风险依赖。
  - 明确第一里程碑和后续跨层功能的边界。
- 测试流程：
  1. 检查必需章节完整。
  2. 检查 Markdown 围栏和行尾空白。
  3. 确认未修改 UI 代码。
- Commit message：
  ```text
  docs: 设计专业视频工作台 UI 重构架构

  - 明确深色终端视觉系统和目标页面结构
  - 划分 UI、应用层和渲染器职责边界
  - 规划主题、布局、状态展示和运行指标阶段
  ```

### [x] Phase 0.2：创建独立 UI 重构 TODO

- 目标：将架构文档拆为可验证、可追踪的最小开发步骤。
- 修改文件：
  - `doc/TODO-Fincept风格UI重构.md`
- 验收标准：
  - 所有 Phase 均包含目标、修改文件、验收标准、测试流程和 Commit message。
  - 当前进度、本轮范围和已知风险明确。
- 测试流程：
  1. 搜索所有 Phase 和任务状态。
  2. 检查每项是否包含文件、验收、测试和提交信息。
- Commit message：与 Phase 0.1 合并提交。

### [x] Phase 0.3：建立现有控件迁移基线

- 目标：记录当前对象名、职责、目标区域和连接约束，避免布局重排破坏行为。
- 修改文件：
  - `doc/TODO-Fincept风格UI重构.md`
- 验收标准：
  - `mainwindow.cpp` 中使用的现有 UI 对象全部进入迁移表。
  - 自动连接的播放、暂停按钮被明确标记。
- 测试流程：
  1. 使用 `rg -n "ui->[A-Za-z0-9_]+" src/ui/mainwindow.cpp` 提取对象名。
  2. 与迁移表逐项比对。
- Commit message：与 Phase 0.1 合并提交。

### [x] Phase 0.4：确认第一里程碑文件范围

- 目标：限制主题和布局改造范围，避免无关重构。
- 修改文件：
  - `doc/UI重构架构指导.md`
  - `doc/TODO-Fincept风格UI重构.md`
- 验收标准：
  - Phase 1 不修改解码、同步、领域层、视觉算法或配置。
  - 确认新增主题源码会被现有 CMake 自动收集。
- 测试流程：
  1. 检查根 CMake 的 `file(GLOB_RECURSE PROJECT_SOURCES ...)`。
  2. 检查当前 Git 状态并记录所有已有改动。
- Commit message：与 Phase 0.1 合并提交。

---

## 5. Phase 1：主题基础

### [x] Phase 1.1：创建独立 UiTheme 模块

- 目标：集中维护原创深色终端主题，禁止在 `MainWindow` 中散落大段 QSS。
- 修改文件：
  - `src/ui/theme/UiTheme.h`，新增
  - `src/ui/theme/UiTheme.cpp`，新增
- 实现要求：
  - 提供单一主题应用入口。
  - 颜色使用语义令牌替换模板占位符。
  - 覆盖窗口、菜单、状态栏、按钮、输入框、下拉框、数字框、列表、标签页、分组框、滑块、滚动条、复选框、单选框、分割器和工具提示。
  - 使用系统中文 UI 字体，不引入外部字体。
- 验收标准：
  - 主题模块不依赖 Controller、领域层或基础设施层。
  - 不包含 Fincept 品牌、源码或资源。
  - 控件默认、悬停、按下、选中、禁用和聚焦状态可区分。
- 测试流程：
  1. 静态搜索确认 QSS 只位于主题模块。
  2. 检查所有模板占位符都有对应替换。
  3. 检查头文件依赖和命名空间边界。
- Commit message：
  ```text
  feat(ui): 建立可复用深色终端主题

  - 新增 UiTheme 主题入口和语义颜色令牌
  - 统一 Qt Widgets 常用控件交互状态
  - 使用系统字体并保持主题模块无业务依赖
  ```

### [x] Phase 1.2：在应用入口接入全局主题

- 目标：在创建 `MainWindow` 前将主题应用到整个 `QApplication`。
- 修改文件：
  - `src/ui/main.cpp`
- 验收标准：
  - 主题在 `QApplication` 创建后、`MainWindow` 创建前应用。
  - 日志初始化、事件循环和退出清理顺序不变。
- 测试流程：
  1. 静态检查 `UiTheme` include 和调用位置。
  2. 确认没有新增第二个 `QApplication` 或全局状态对象。
- Commit message：与 Phase 1.1 合并提交。

### [x] Phase 1.3：统一产品标题

- 目标：将窗口标题从历史名称统一为 `TinyVideoProcessor`。
- 修改文件：
  - `src/ui/mainwindow.cpp`
- 验收标准：
  - 运行时窗口标题为 `TinyVideoProcessor`。
  - 不修改播放初始化、信号连接或状态逻辑。
- 测试流程：
  1. 搜索 `modernFFplay-V2.0`，确认历史窗口标题被移除。
  2. 搜索 `setWindowTitle`，确认标题入口唯一明确。
- Commit message：与 Phase 1.1 合并提交。

### [x] Phase 1.4：完成主题阶段静态验收

- 目标：确认 Phase 1 只改变全局视觉基础，不破坏行为和文件边界。
- 修改文件：
  - `doc/TODO-Fincept风格UI重构.md`
- 验收标准：
  - `git diff --check` 无错误。
  - Git 状态只包含 Phase 0、Phase 1 允许文件。
  - 未修改 `CMakeLists.txt`、渲染器、Controller、领域层和基础设施层。
- 测试流程：
  1. 执行定向 `git diff --check`。
  2. 执行 `git status --short`。
  3. 使用 `rg` 检查主题调用、窗口标题和样式入口。
  4. 默认不编译；若用户要求，再执行现有 Qt 构建。
- Commit message：与 Phase 1.1 合并提交。

---

## 6. Phase 2：主窗口布局重构

### [x] Phase 2.1：建立顶部产品状态区与命令区

- 目标：将产品标识、媒体源添加、全局播放和布局切换从混合区域中分离。
- 修改文件：
  - `src/ui/mainwindow.ui`
  - `src/ui/mainwindow.cpp`，仅用于初始化真实状态和必要的鼠标跟踪调整
- 验收标准：
  - 顶部命令区包含添加文件、摄像头、RTSP、全部启动/暂停/停止和布局切换。
  - 不展示伪造的 FPS、时间或容量数据。
  - 所有原控件对象名和连接保持有效。
- 测试流程：
  1. 对照控件迁移表检查对象名。
  2. 检查全局按钮的显式连接仍存在。
  3. 人工验证添加源和全部控制操作。
- Commit message：
  ```text
  feat(ui): 重构专业视频工作台主布局

  - 增加顶部产品状态与全局命令区域
  - 建立左右侧栏和中央多视频工作区
  - 将播放控制和高级处理参数按使用层级拆分
  ```

### [x] Phase 2.2：建立左侧媒体与视频源区域

- 目标：明确文件播放列表与多路视频源列表的展示边界。
- 修改文件：
  - `src/ui/mainwindow.ui`
  - `src/ui/mainwindow.cpp`，仅在布局变化要求调整事件过滤时修改
- 验收标准：
  - `listWidget_files` 和 `listWidget_streams` 均保留。
  - 文件列表与多路流列表的标题和操作语义清晰。
  - 移除、活动视频和活动音频按钮位于视频源上下文中。
- 测试流程：
  1. 验证文件列表双击、上一项、下一项和播放模式。
  2. 验证流列表选择、双击、移除、活动视频和活动音频。
- Commit message：与 Phase 2.1 合并提交。

### [x] Phase 2.3：建立中央视频区与右侧属性面板

- 目标：保持视频区主体地位，并将高级参数迁移到当前视频属性区。
- 修改文件：
  - `src/ui/mainwindow.ui`
  - `src/ui/mainwindow.cpp`
- 验收标准：
  - `widget_videoArea` 和 `gridLayout_videoArea` 保持唯一统一渲染器挂载点。
  - 预览帧率、图像处理和视觉分析控件进入右侧面板。
  - 无活动视频时沿用现有禁用逻辑。
- 测试流程：
  1. 检查渲染器初始化和挂载代码未改变所有权。
  2. 验证处理模式、阈值、视觉管线和识别叠加状态同步。
- Commit message：与 Phase 2.1 合并提交。

### [x] Phase 2.4：收口底部播放区和状态区

- 目标：底部只保留高频播放操作和真实状态摘要，全局停止仍由顶部命令区承担。
- 修改文件：
  - `src/ui/mainwindow.ui`
  - `src/ui/mainwindow.cpp`
- 验收标准：
  - 上一项、播放、暂停、下一项、进度、时间、速度、音量、静音和全屏清晰排列。
  - `pushButton_stopAll` 保持顶部“全部停止”语义，不伪装为底部单路停止。
  - 实时流仍显示 `LIVE` 并禁用 Seek。
  - `pushButton_play` 和 `pushButton_pause` 自动连接继续生效。
- 测试流程：
  1. 静态检查自动连接槽和对象名。
  2. 人工验证文件源与实时源的进度区差异。
- Commit message：与 Phase 2.1 合并提交。

### [x] Phase 2.5：布局阶段静态验收

- 目标：确认布局迁移未改变业务状态和渲染所有权。
- 修改文件：
  - `doc/TODO-Fincept风格UI重构.md`
- 验收标准：
  - 所有迁移控件均能在 `.ui` 中找到且对象名唯一。
  - `MainWindow` 未新增基础设施层依赖。
  - 不存在明显的固定尺寸冲突或横向单行堆叠。
- 测试流程：
  1. 解析 `.ui` 控件树并与迁移表比对。
  2. 执行 `git diff --check` 和定向 `rg`。
  3. 用户要求时编译运行并检查 100%、125%、150% DPI。
- Commit message：与 Phase 2.1 合并提交。

静态验收记录（2026-07-17）：

- `mainwindow.ui` 已通过 UTF-8 XML 解析，共 96 个具名 widget/layout/spacer 对象且名称唯一。
- Qt 5.15.2 `uic` 已成功解析并生成界面代码，退出码为 0。
- `mainwindow.cpp` 的 37 个 `ui->` 对象引用均能在 `.ui` 中找到；播放/暂停自动连接对象名保持不变。
- `widget_videoArea` 与 `gridLayout_videoArea` 仍是唯一渲染挂载链，`MultiViewRenderer` 的父对象和所有权代码未改变。
- 状态栏只显示初始化结果和 Controller 的真实停止/播放/暂停状态，未写死 FPS、容量或媒体指标。
- 两个预览帧率单选按钮的直接父对象均为 `tab_preview`，保留 `autoExclusive` 行为。
- 全局 QSS 的 13 个语义 token 均有对应替换项，未直接选择 `QOpenGLWidget` 或运行时渲染器。
- 新的递归鼠标跟踪在 `MultiViewRenderer` 创建后执行，覆盖顶部、左右面板、页签子控件和运行时渲染器；事件过滤器仍只观察鼠标移动并继续默认分发。
- `git diff --check` 无错误，仅报告工作区已有的 LF 到 CRLF 提示。
- 按默认开发规范未编译、未运行；全屏显示范围、焦点控件下快捷键和 100%/125%/150% DPI 仍需后续人工验收。

---

## 7. Phase 3：视频源列表与状态表达

### [x] Phase 3.1：定义 Presentation Layer 展示状态

- 目标：在不复制领域状态的前提下整理视频源列表所需的只读展示字段。
- 修改文件：
  - `src/ui/mainwindow.h`
  - `src/ui/mainwindow.cpp`
  - `src/ui/StreamPresentationState.h`，新增
- 验收标准：
  - 展示状态只包含 UI 所需值，不持有媒体对象。
  - 所有值来自现有 Controller 或 UI 已有映射。
- 测试流程：
  1. 检查未引入领域层内部对象。
  2. 检查流移除后展示状态同步清理。
- Commit message：
  ```text
  feat(ui): 完善视频源状态列表

  - 增加活动视频和活动音频状态表达
  - 统一文件、摄像头和 RTSP 条目样式
  - 保持列表展示状态不拥有媒体对象
  ```

### [x] Phase 3.2：实现紧凑视频源条目

- 目标：通过委托或轻量条目组件显示名称、类型和状态。
- 修改文件：
  - `src/ui/StreamListItemDelegate.h`，新增
  - `src/ui/StreamListItemDelegate.cpp`，新增
  - `src/ui/mainwindow.cpp`
- 验收标准：
  - 活动视频、活动音频、LIVE、已加载和最近错误状态可区分。
  - 状态同时使用文字或图标，不只依赖颜色。
  - Delegate 不访问 Controller，不创建逐行 QWidget；16 路刷新按 StreamId 原位更新。
  - 逐流播放状态明确推迟到应用层只读状态边界具备后实现，不展示推断值。
- 测试流程：
  1. 静态检查委托不访问 Controller。
  2. 人工验证不同来源和状态组合。
- Commit message：与 Phase 3.1 合并提交。

### [x] Phase 3.3：视频源交互静态回归检查

- 目标：确认选中、双击、移除、活动视频和活动音频行为不退化。
- 修改文件：
  - `doc/TODO-Fincept风格UI重构.md`
- 验收标准：
  - 选择条目只改变选择态。
  - 双击行为继续按现有语义激活视频。
  - 移除活动流后 UI 状态正确回收。
- 测试流程：
  1. 静态检查选择、双击、移除、活动视频和活动音频连接未改变。
  2. 用户要求运行时，对至少四路混合来源重复执行选择、激活和移除。
  3. 运行验证活动视频与活动音频标志独立。
- Commit message：与 Phase 3.1 合并提交。

静态验收记录（2026-07-17）：

- 新增纯展示 `StreamPresentationState`/`StreamPresentationMetadata`，仅保存名称、来源类型、实时属性和最近错误，不持有媒体对象。
- 新增单个 `QStyledItemDelegate` 绘制来源、主画面、音频、LIVE、已加载和最近错误文字徽章；Delegate 不访问 Controller，也不创建逐行 QWidget。
- `RefreshStreamView()` 已由整表清空重建改为按 `StreamId` 原位更新，`Qt::UserRole` 继续保存 StreamId，选择只在原条目失效时回退并滚动。
- 本地文件、播放列表切换、摄像头和 RTSP 四条成功创建路径均登记来源；`StreamRemoved` 同步清理展示元数据。
- 活动视频、活动音频和有无音频仍从 Controller 实时读取；没有逐流接口的播放状态未被推断或展示。
- `StreamErrorOccurred` 只记录有效流的“最近错误”，无效 StreamId 不绑定列表，错误元数据随流移除清理。
- 选择、`currentRowChanged`、双击激活、移除、设为主画面和设为音频源的原有连接未改变。
- `git diff --check` 无错误；新增/修改文件无行尾空白，Delegate 与主窗口花括号数量平衡。
- 按默认开发规范未编译、未运行；窄侧栏、高 DPI、错误触发和 16 路刷新仍需后续人工验收。

---

## 8. Phase 4：多视口视觉融合

### [x] Phase 4.1：统一多视口边框主题

- 目标：将活动视频和普通槽位边框调整为新主题语义色。
- 修改文件：
  - `src/infrastructure/rendering/multiviewrenderer.cpp`
- 验收标准：
  - 活动画面使用琥珀色强调，普通画面使用弱边框。
  - 四种布局和空槽位绘制正确。
- 测试流程：
  1. 静态检查只修改直接视觉常量。
  2. 人工切换四种布局和活动视频。
- Commit message：
  ```text
  feat(ui): 融合多视口主题与视频状态浮层

  - 统一活动画面和普通槽位边框语义
  - 增加不影响点击命中的视频信息浮层
  - 保持多布局和宽高比计算不变
  ```

### [x] Phase 4.2：预研视频信息浮层方案

- 目标：在 `QPainter + 原生 OpenGL` 与独立覆盖层控件之间选择安全方案。
- 修改文件：
  - 新建独立预研文档或更新本 TODO
- 验收标准：
  - 说明实现方式、优缺点、风险、修改范围和推荐理由。
  - 用户确认后才修改渲染器结构。
- 测试流程：评审绘制顺序、DPI、点击命中和生命周期。
- Commit message：与 Phase 4.1 分开提交预研文档。

预研决策（2026-07-17）：

- `QPainter + 原生 OpenGL` 混合绘制可行，但每帧需要 Painter/OpenGL 状态切换，并会扩大渲染器展示接口和 16 路性能验收范围。
- OpenGL 字体/纹理方案涉及中文字形、DPI、混合和上下文生命周期，首版不采用。
- 采用 UI 层透明 `StreamViewportOverlay` 子控件：通过 `Viewports()` 使用逻辑坐标，设置鼠标穿透和无焦点，保持 OpenGL 帧路径与命中逻辑不变。
- 覆盖层由 `MainWindow` 在现有刷新链路中整表传入 `StreamPresentationState`；渲染器不依赖 UI 类型。
- 用户已确认进入 Phase 4，按该最小边界继续 Phase 4.3；跨平台层叠、缩放和高 DPI 仍列入运行验收。

### [x] Phase 4.3：实现真实可用的视频浮层

- 目标：显示现有可获得的视频名称和状态，后续接入真实指标。
- 修改文件：
  - `src/ui/StreamViewportOverlay.h`，新增
  - `src/ui/StreamViewportOverlay.cpp`，新增
  - `src/ui/mainwindow.h`
  - `src/ui/mainwindow.cpp`
- 验收标准：
  - 浮层不影响帧绘制、点击、双击和宽高比。
  - 缺失数据使用 `--` 或不展示。
- 测试流程：
  1. 验证 Single、2x2、3x3、4x4。
  2. 验证 DPI、缩放、聚焦和空槽位。
- Commit message：与 Phase 4.1 合并或按方案独立提交。

静态验收记录（2026-07-17）：

- `MultiViewRenderer` 只调整 15 个 RGB 常量：背景/空槽位 `#070A0D`、已绑定槽位 `#0D1217`、普通边框 `#202A33`、活动边框 `#F59E0B`；2/4 framebuffer 物理像素宽度和全部绘制函数未改变。
- 新增 UI 层 `StreamViewportOverlay`，作为渲染器子控件使用 `Viewport::qtRect` 逻辑坐标绘制，不读取 framebuffer 坐标，不修改 `paintGL()` 或 OpenGL 状态。
- 覆盖层设置鼠标穿透和无焦点，父对象负责生命周期；Resize/Show 事件同步几何，整表状态刷新时主动置顶和重绘。
- 浮层只显示现有名称、FILE/CAM/RTSP、LIVE 实时源属性、主画面、活动音频和最近错误；未展示逐流播放状态、FPS、分辨率、编码或在线状态。
- `MainWindow` 复用 `BuildStreamPresentationState()`，在既有 `RefreshStreamView() -> RefreshMultiViewRenderer()` 链路中整表更新；添加、移除、活动视频、活动音频、错误、布局和聚焦路径均会刷新。
- 4x4 窄视口下优先保留省略后的视频名称，空间不足时隐藏来源徽章；标题高度按实际字体度量，状态徽章不足空间时不绘制。
- 默认 QWidget 父内容传播负责透明区域，覆盖层不使用 `CompositionMode_Source` 清屏，避免覆盖 QOpenGLWidget 已合成的视频内容。
- 基础设施层没有依赖 `StreamPresentationState` 或浮层类型；新增文件由现有 `GLOB_RECURSE CONFIGURE_DEPENDS` 自动收录，无需修改 CMake。
- `git diff --check` 无错误；相关新增/修改文件无行尾空白，花括号和圆括号数量平衡。
- 按默认开发规范未编译、未运行；透明层叠、连续帧、最小化恢复、全屏、跨屏 DPI、100%/125%/150% 缩放和四种布局仍需后续人工验收。

---

## 9. Phase 5：真实运行指标

### [x] Phase 5.1：梳理指标所有权

- 目标：明确 FPS、分辨率、编码格式、丢包率和推理状态的真实来源。
- 修改文件：
  - `doc/UI重构架构指导.md`
  - `doc/TODO-Fincept风格UI重构.md`
  - `doc/视频运行指标只读数据链路.md`，新增
- 验收标准：
  - 每个字段都有唯一所有者、采样频率和生命周期。
  - 列出跨层修改文件、风险和替代方案。
- 测试流程：已完成 Controller/管线/调度器、渲染器统计和 UI 生命周期三路静态设计审查；用户已确认进入 Phase 5。
- 设计结论：
  - 累计提交帧、调度丢帧和最近帧尺寸由 `VideoFrameScheduler` 唯一拥有。
  - 源尺寸、编码和处理状态由 `MediaPipeline` 按值合并，Manager 按 `StreamId` 整表汇总，Controller 映射为 UI DTO。
  - `MainWindow` 使用唯一 1 Hz 粗粒度定时器求采样窗口差值；状态切换可即时拉取，短于 500 ms 的窗口保留原基线，首样本和生命周期重建显示 `--`。
  - FPS 的准确语义为预览输出链提交速率；调度丢帧不得冒充网络丢包。
  - 网络丢包率、推理吞吐和逐流播放状态当前没有可靠数据源，明确显示不可用或不展示。
  - 本阶段不修改 `MultiViewRenderer`，不把指标采样放入 `paintGL()` 或渲染锁。
- Commit message：
  ```text
  docs: 设计视频运行指标只读数据链路
  ```

### [x] Phase 5.2：建立应用层只读状态边界

- 目标：让 UI 不接触基础设施对象即可获得低频指标。
- 修改文件：
  - `src/application/MediaPlayController.h/.cpp`
  - `src/domain/MultiStreamManager.h/.cpp`
  - `src/domain/MediaPipeline.h/.cpp`
  - `src/infrastructure/sync/videoframescheduler.h/.cpp`
- 验收标准：
  - 指标为只读快照或低频信号。
  - 流移除和窗口关闭后没有悬空更新。
- 测试流程：
  1. 静态检查信号生命周期和线程边界。
  2. 用户要求时执行多路运行压力测试。
- 实现结果：
  - 调度器使用管线生命周期内单调的原子累计计数，记录预览提交帧、调度丢帧和最近真实帧尺寸；暂停恢复不清零。
  - `MediaPipeline` 在初始化时复制源尺寸和编码，并合并处理链状态；Manager 按 `StreamId` 整表汇总。
  - Controller 将 FFmpeg codec id 映射为只读字符串 DTO；UI 未获得媒体源、管线、调度器或渲染器指针。
  - Manager 与 Controller 接口注释明确 owner-thread 调用约束；流移除后整表自然不再包含对应 ID。
- Commit message：
  ```text
  feat(ui): 接入真实视频运行指标

  - 增加应用层只读状态快照
  - 低频发布 FPS、格式和处理状态
  - 避免 UI 直接访问播放基础设施
  ```

### [x] Phase 5.3：将真实指标接入 UI

- 目标：在顶部、浮层、右侧基本信息和状态栏中按层级展示真实数据。
- 修改文件：
  - `src/ui/mainwindow.ui`
  - `src/ui/mainwindow.h/.cpp`
  - `src/ui/StreamPresentationState.h`
  - `src/ui/StreamViewportOverlay.cpp`
- 验收标准：
  - 不重复展示同一信息。
  - 不写死数据。
  - 刷新不会阻塞 UI 或渲染线程。
- 测试流程：默认执行多路添加、移除、断流、切换和关闭生命周期静态检查；用户要求时再编译运行。
- 实现结果：
  - 顶部复用工作区标签展示流数、出帧路数和已采样流的合计预览提交 FPS。
  - 浮层按空间展示真实/源分辨率和单路预览提交 FPS；字体或状态徽章空间不足时优先保留名称与徽章。
  - 右侧展示活动流 codec、调度丢帧率、图像处理和视觉分析；网络丢包与推理吞吐明确显示“不可用”。
  - 状态栏永久摘要只展示活动视频、音频和视觉管线上下文，不重复技术指标。
  - 唯一 1 Hz `Qt::CoarseTimer` 拉取整表；状态切换即时刷新，小于 500 ms 的窗口保留原速率基线。
  - 流移除时同步删除展示与采样基线；关闭和析构均先停止指标定时器，再执行 Controller `Shutdown()`。
- 静态验收：
  - Qt 5.15.2 `uic` 成功解析 `mainwindow.ui` 并生成 728 行界面代码。
  - `.ui` 共 98 个 widget/layout/spacer 对象且名称唯一；`mainwindow.cpp` 的 UI 对象引用除 `setupUi` 方法外均可解析到对象名。
  - 相关 C++ 头文件/实现的花括号与圆括号数量平衡，全部 16 个本阶段文件无行尾空白。
  - 三路交叉复核确认 API/线程边界、字段真实性、采样数学、生命周期和浮层空间降级无新增阻断问题。
  - `git diff --check` 无错误，仅报告工作区既有的 LF 到 CRLF 提示。
  - 按默认开发规范未编译、未运行；真实多路 FPS、暂停恢复、断流、跨 DPI 和性能影响仍需后续人工验收。
- Commit message：与 Phase 5.2 合并提交。

---

## 10. Phase 6：关联对话框与细节收口

> 实施状态：已完成静态验收。对话框直接继承应用级主题；本阶段仅修改下列直接相关文件，未新增局部主题字符串。

### [x] Phase 6.1：统一摄像头设置对话框

- 目标：让摄像头设置对话框使用相同主题、间距和按钮语义。
- 修改文件：
  - `src/ui/CameraSettingsDialog.h`
  - `src/ui/CameraSettingsDialog.cpp`
- 验收标准：
  - 设备、格式、帧率、刷新、确定和取消功能不变。
  - 对话框不维护额外主题字符串。
  - 设备或格式不可用时，下级选项无陈旧内容且原因可见；刷新入口始终可用。
- 测试流程：人工验证刷新设备、选择参数、确定和取消。
- Commit message：
  ```text
  feat(ui): 统一摄像头设置与工作台视觉
  ```

### [x] Phase 6.2：收口文案、空状态和工具提示

- 目标：移除遗留混合文案并完善禁用、空状态和错误提示。
- 修改文件：
  - `src/ui/mainwindow.ui`
  - `src/ui/mainwindow.cpp`
  - `src/ui/CameraSettingsDialog.cpp`
- 验收标准：
  - 操作文案以中文为主，技术术语保留英文。
  - 禁用控件和空状态原因清晰。
  - 列表空状态不使用会干扰行号或视频流 ID 的占位条目。
- 测试流程：静态搜索遗留英文操作文案并人工检查空状态。
- 静态验收：
  - 摄像头、主窗口和范围三路只读复核通过；未发现刷新、首项选择、确定/取消连接以及播放、多路控制语义退化。
  - Qt 5.15.2 `uic` 成功解析 `mainwindow.ui`；XML 共 98 个 UI 对象且名称唯一，`mainwindow.cpp` 的 45 个 `ui->` 对象引用均可解析。
  - 遗留英文扫描仅剩品牌、协议、状态缩写、快捷键、编码/模型等技术内容和内部日志。
  - 六个本阶段文件 `git diff --check` 无错误，仅报告工作区既有的 LF 到 CRLF 提示。
  - 按默认开发规范未编译、未运行；摄像头枚举与刷新、Enter/Esc、不同 DPI 及完整工作台交互仍需 Phase 7 人工验证。
- Commit message：与 Phase 6.1 合并提交。

---

## 11. Phase 7：最终验收

> 实施状态：已完成。用户已授权编译、运行、截图、提交和推送；验收报告与真实运行截图已纳入仓库，摄像头、RTSP、多 DPI 和批量 CTest 的环境限制已如实记录。

### [x] Phase 7.1：静态范围与架构检查

- 目标：确认实现与架构文档、TODO 和授权范围一致。
- 修改文件：
  - `doc/TODO-Fincept风格UI重构.md`
  - `doc/UI重构架构指导.md`
  - `doc/UI重构验收报告.md`
- 验收标准：
  - `git diff --check` 无错误。
  - 无无关文件修改。
  - UI 不直接控制解码、同步或线程生命周期。
- 测试流程：执行全量定向静态检查和文件范围审计。
- 完成记录（2026-07-17）：
  - `dev` 基线上的实现修改均落在已确认的 Phase 0～6 文件范围；Phase 7 仅增加报告、截图并更新主线文档。
  - `git diff --check` 无空白错误；Qt 6.5.3 `uic` 成功解析 98 个唯一 UI 对象，45 个 `ui->` 引用均可解析。
  - UI 未出现直连解码、同步、音频或线程队列的新调用；真实指标继续通过 Controller 只读边界进入 UI。
  - Qt Creator 自动修改的 `CMakeLists.txt.user` 明确保持未暂存、未提交。
- Commit message：
  ```text
  feat(ui): 完成专业视频工作台视觉收口与验收
  ```

### [x] Phase 7.2：运行验收与截图

- 目标：在用户明确要求后编译运行并保存最终展示证据。
- 修改文件：
  - `doc/UI重构验收报告.md`
  - `doc/images/ui-refactor/phase7-workspace.png`
- 验收标准：
  - 覆盖本地文件、摄像头、RTSP、四种布局、图像处理和视觉分析。
  - 覆盖全屏、活动视频、活动音频、移除流和关闭窗口。
- 测试流程：按架构文档第 13.2 节执行完整人工测试。
- 完成记录（2026-07-17）：
  - Qt 6.5.3 + MSVC Debug 主程序编译通过；4 个单元测试逐项运行全部通过。批量 CTest 的固定超时现象单独记录为运行器限制。
  - 本地视频的播放、暂停、Seek、1.5x、音量、静音、四种布局、全局启停、全屏、活动视频/音频和移除回退均通过。
  - 4 路本地视频在 `2×2` 下全部真实出帧；最终截图同时展示 OpenCV 视觉分析和识别叠加层，窗口多次以退出码 0 干净结束。
  - 摄像头对话框检测到 1 台设备且参数有效，但未擅自启动实际采集；RTSP 因无服务端只验证默认地址和取消路径；当前系统完成 175% DPI 验收。
  - 详细证据、未覆盖项和残余风险见 `doc/UI重构验收报告.md`。
- Commit message：与 Phase 7.1 合并提交。

---

## 12. 沟通与暂停条件

出现以下情况时必须暂停并与用户沟通：

- 需要修改当前 Phase 未列出的文件。
- 需要改变播放、活动视频、活动音频、视觉管线或渲染生命周期语义。
- 需要在 `QPainter + OpenGL` 与覆盖层控件之间做结构性选择。
- 需要新增第三方 UI、字体或图标依赖。
- 需要为真实运行指标修改应用层或基础设施层。
- 静态检查发现现有架构文档或 TODO 与实际代码不一致。
- 修改可能影响数据兼容、公共接口或已有播放行为。
