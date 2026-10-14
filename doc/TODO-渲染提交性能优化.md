# TODO-渲染提交性能优化

## 当前进度

- 已完成：Phase 0 架构设计、Phase 1 转换前帧合并、Phase 2 诊断指标、Phase 3.1 静态验收、Phase 4 NV12 双平面直接渲染与静态验收。
- 进行中：等待 Phase 3.2 更新后的真实 120 FPS 运行回归。
- 未完成：Phase 3.2 更新后运行回归。
- 已知风险：GUI 线程仍承担调度和 OpenGL 绘制。
- 本轮范围：`MultiViewRenderer` 头文件、实现文件及本专题文档。

## Phase 0：架构与修改边界

- [x] Phase 0.1：创建独立架构指导与 TODO
  - 目标：固定转换前合并方案、帧所有权和清理语义。
  - 修改文件：
    - `doc/渲染提交性能优化架构指导.md`
    - `doc/TODO-渲染提交性能优化.md`
  - 验收标准：明确两级帧状态、转换失败策略和不修改范围。
  - 测试流程：静态对照现有 `PresentFrame()`、`paintGL()` 和资源释放路径。
  - Commit message：与 Phase 1 合并提交。

## Phase 1：转换前最新帧合并

- [x] Phase 1.1：增加待转换帧状态
  - 目标：每路最多保留一张尚未转换的最新输入帧。
  - 修改文件：`src/infrastructure/rendering/multiviewrenderer.h`。
  - 验收标准：待转换帧与当前显示帧职责分离，所有权均由 RAII 管理。
  - 测试流程：检查接收、替换、清理和流移除路径。
  - Commit message：`perf(render): 合并高帧率待显示帧并延迟格式转换`

- [x] Phase 1.2：将格式转换移入实际绘制路径
  - 目标：同一轮绘制前只转换每路最新帧。
  - 修改文件：`src/infrastructure/rendering/multiviewrenderer.cpp`。
  - 验收标准：`PresentFrame()` 不调用转换器；绘制前按需转换；失败时保留旧画面。
  - 测试流程：定向检查 `ConvertToDisplayFrame` 调用位置和 `textureDirty` 更新位置。
  - Commit message：与 Phase 1.1 合并提交。

## Phase 2：性能诊断

- [x] Phase 2.1：补充合并与转换计数
  - 目标：从日志判断优化是否减少无效转换。
  - 修改文件：
    - `src/infrastructure/rendering/multiviewrenderer.h`
    - `src/infrastructure/rendering/multiviewrenderer.cpp`
  - 验收标准：日志包含每路和窗口级 `received/coalesced/converted/uploads`。
  - 测试流程：检查累计值与窗口差值计算，空闲状态不刷日志。
  - Commit message：与 Phase 1 合并提交。

## Phase 3：验收

- [x] Phase 3.1：静态验收
  - 目标：确认修改范围、空白、接口和生命周期安全。
  - 修改文件：本专题全部文件。
  - 验收标准：`git diff --check` 通过；公共接口、调度器和 UI 未修改。
  - 测试流程：执行 `git diff --check`、`git status --short` 和定向 `rg`。
  - 完成记录：
    - `git diff --check` 通过，仅有仓库既有的 LF 到 CRLF 转换提示。
    - `ConvertToDisplayFrame()` 已从 `PresentFrame()` 移到实际绘制准备函数。
    - `ClearFrame()`、`DetachStream()` 与析构均复用统一帧释放路径。
    - 头文件与实现文件花括号数量平衡，公共接口未变化。
    - 修改范围仅包含 `MultiViewRenderer` 及本专题文档。
    - 按默认开发规范未编译、未运行，真实性能改善待 Phase 3.2 验证。
  - Commit message：与 Phase 1 合并提交。

- [ ] Phase 3.2：120 FPS 真实素材回归
  - 目标：确认调度丢帧率及 GUI 响应改善。
  - 修改文件：验收后只更新本 TODO。
  - 验收标准：记录同一素材优化前后的丢帧率和渲染日志；暂停、Seek、切源和关闭无回退。
  - 测试流程：保持图像处理和视觉分析关闭，运行 30/120 FPS 素材，再分别开启功能对照。
  - 已记录基线：转换前合并优化后，120 FPS 素材 Debug 约 5%，Release 约 3%；NV12 直接渲染后的数据待补充。
  - Commit message：`test(render): 完成高帧率渲染提交优化验收`

## Phase 4：NV12 双平面直接渲染

- [x] Phase 4.1：扩展纹理上传描述
  - 目标：同时描述 YUV420P 三平面和 NV12 双平面数据。
  - 修改文件：`src/infrastructure/rendering/multiviewrenderer.cpp`。
  - 验收标准：NV12 UV 平面按 RG8 纹理上传，行长度按双通道像素换算；YUV420P 路径保持不变。
  - 测试流程：定向检查 plane 数量、纹理格式、奇数尺寸和 linesize 防护。
  - Commit message：`perf(render): 使用双平面纹理直接渲染 NV12 帧`

- [x] Phase 4.2：增加 NV12 Shader 与色彩参数
  - 目标：在 GPU 中完成 NV12 到 RGB 转换。
  - 修改文件：
    - `src/infrastructure/rendering/multiviewrenderer.h`
    - `src/infrastructure/rendering/multiviewrenderer.cpp`
  - 验收标准：支持有限/全范围以及 BT.601、BT.709、BT.2020 系数；Shader 失败可安全回退。
  - 测试流程：静态检查 uniform 绑定、纹理单元和格式选择；运行时用标准色条或已知素材比对颜色。
  - Commit message：与 Phase 4.1 合并提交。

- [x] Phase 4.3：接入直接帧准备与回退路径
  - 目标：NV12 不调用 `VideoFrameConverter`，其他格式保持兼容。
  - 修改文件：`src/infrastructure/rendering/multiviewrenderer.cpp`。
  - 验收标准：NV12 显示格式保持 NV12，YUV420P 与其他软件格式仍可显示；公共接口不变。
  - 测试流程：检查 `PreparePendingFrameForDisplay()` 的格式分支和性能日志。
  - Commit message：与 Phase 4.1 合并提交。

- [x] Phase 4.4：静态验收
  - 目标：确认资源生命周期、格式切换和修改范围安全。
  - 修改文件：本专题全部文件。
  - 验收标准：`git diff --check` 通过；纹理格式变化会触发重建；清理路径释放全部纹理；未修改调度和解码模块。
  - 测试流程：执行差异、状态、花括号和定向 `rg` 检查。
  - 完成记录：
    - `git diff --check` 通过，仅有仓库既有的 LF 到 CRLF 转换提示。
    - NV12 使用 R8 Y 纹理和 RG8 UV 纹理，UV 行长度按双通道像素换算。
    - NV12 Shader 支持有限/全范围和 BT.601、BT.709、BT.2020 系数。
    - RG 纹理或 Shader 不可用时，NV12 继续通过 `VideoFrameConverter` 回退为 YUV420P。
    - 像素格式、纹理尺寸或纹理内部格式变化都会触发纹理重建。
    - 三张纹理仍由统一 `FreeTextures()` 释放，公共接口未变化。
    - C++ 头文件与实现文件花括号平衡，三个 Shader 原始字符串边界平衡。
    - 未修改调度器、解码器、同步时钟和 UI；用户运行产生的 `CMakeLists.txt.user` 修改未触碰。
    - 按默认开发规范未编译、未运行，真实性能与色彩表现待 Phase 3.2 验证。
  - Commit message：与 Phase 4.1 合并提交。

## 风险与沟通点

- 如果真实回归后仍有明显同步丢帧，下一步应单独设计调度线程迁移，不能在本轮直接跨线程调用非线程安全的图像与视觉配置对象。
- NV12 直接渲染需要运行时比对色彩；若素材元数据错误，默认色彩策略可能与原转换路径存在视觉差异。
- 默认开发规范不编译、不运行；本轮先完成静态验收。
