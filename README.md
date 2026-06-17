# TinyVideoProcessor

基于 Qt 6 和 FFmpeg 的渐进式视频处理项目。当前项目已经从早期播放器演进到分层架构：UI 层只负责交互，应用层负责播放状态编排，领域层通过 `MediaPipeline` 管理单路播放管线，后续会在此基础上扩展多路视频管理、摄像头和网络流。

## 当前状态

当前主线处于 **Phase 3.2：多路管理架构设计与落地准备**。

- Phase 1 播放器功能完善已完成：播放控制、Seek、倍速、音量、静音、全屏、播放列表等能力已接入。
- Phase 3.0 无音频流视频支持已完成：无音频视频可以进入播放链路，不再强依赖音频流。
- Phase 3.1 单路 `MediaPipeline` 抽取已完成：播放管线资源已经从 `MediaPlayController` 下沉到 `MediaPipeline`。
- Phase 3.2 已完成预研和 TODO 拆分：下一步引入 `MultiStreamManager`，管理多个单路 `MediaPipeline`。

## 当前能力

- 本地视频文件播放。
- 音视频同步。
- 无音频流视频播放。
- 播放、暂停、停止、自然播放结束处理。
- 绝对 Seek 和相对 Seek。
- 快进、后退。
- 播放速度控制。
- 音量控制和静音控制。
- 播放列表管理：添加、移除、清空、上一项、下一项、播放模式。
- 全屏播放和全屏鼠标隐藏。
- 基于 `IMediaSource` 的媒体源抽象。
- 基于 `FileMediaSource` 的文件媒体源接入。
- 基于 `MediaPipeline` 的单路播放管线封装。

## 架构设计

项目遵循 `doc/架构指导.md` 中的分层路线，当前边界如下：

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
        -> MediaPipeline
            -> IMediaSource / FileMediaSource
            -> demuxer
            -> decoder
            -> AudioPlayer
            -> VideoRender
            -> syncmanager
            -> SyncQueue
```

### 分层职责

- `MainWindow`：UI 层，只处理界面事件、控件状态和用户交互。
- `PlaylistManager`：领域层文件列表管理，只负责播放列表数据和顺序逻辑。
- `MediaPlayController`：应用层播放入口，保存用户可见状态和全局播放参数。
- `MediaPipeline`：领域层单路播放管线，负责单个媒体源的资源创建、线程、队列、解复用、解码、音频输出、视频渲染和生命周期。
- `IMediaSource`：媒体源抽象，当前已有文件源，后续摄像头、网络流、工业相机都应通过该接口扩展。
- `infrastructure`：FFmpeg、音频播放、视频渲染、同步等基础设施实现。

### Phase 3.2 目标架构

Phase 3.2 不直接让 UI 管多个播放管线，也不让 `MediaPipeline` 感知多路编号。目标结构是：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline #1
            -> MediaPipeline #2
            -> MediaPipeline #3
```

关键约束：

- `StreamId` 只属于 `MultiStreamManager` 外层管理。
- `MediaPipeline` 继续保持单路职责，不新增 id 字段。
- `MediaPlayController` 仍是 UI 对外入口。
- 第一版多路先验证文件源多路生命周期，不做复杂多路 UI。
- 第一版多路先使用多个 `VideoRender`，不引入 `MultiViewRenderer`。

## 项目结构

```text
TinyVideoProcessor/
|-- README.md
|-- CMakeLists.txt
|-- Lib/
|   `-- ffmpeg/
|-- doc/
|   |-- 架构指导.md
|   |-- TODO.md
|   |-- Phase1_*.md
|   |-- Phase3_0_无音频流支持实现报告.md
|   |-- Phase3_1_单路MediaPipeline抽取计划.md
|   |-- Phase3_1_单路MediaPipeline抽取实现报告.md
|   |-- Phase3_2_多路管理架构预研报告.md
|   `-- Phase3_2_多路管理实施计划.md
`-- src/
    |-- ui/
    |   |-- main.cpp
    |   |-- mainwindow.h
    |   |-- mainwindow.cpp
    |   `-- mainwindow.ui
    |-- application/
    |   |-- MediaPlayController.h
    |   `-- MediaPlayController.cpp
    |-- domain/
    |   |-- IMediaSource.h
    |   |-- FileMediaSource.h
    |   |-- FileMediaSource.cpp
    |   |-- MediaPipeline.h
    |   |-- MediaPipeline.cpp
    |   |-- PlaylistManager.h
    |   `-- PlaylistManager.cpp
    |-- infrastructure/
    |   |-- demuxer.h / demuxer.cpp
    |   |-- decoder.h / decoder.cpp
    |   |-- audioplayer.h / audioplayer.cpp
    |   |-- videorender.h / videorender.cpp
    |   `-- syncmanager.h / syncmanager.cpp
    `-- core/
        `-- queue.h
```

## 快速开始

### 环境要求

- Windows 10/11。
- Qt 6.5 或更高版本。
- CMake 3.16 或更高版本。
- C++17 编译器。
- 项目内置 FFmpeg 依赖目录：`Lib/ffmpeg`。

当前 CMake 工程目标名为 `modernFFplay`。

### 使用 Qt Creator

1. 使用 Qt Creator 打开根目录下的 `CMakeLists.txt`。
2. 选择 Qt 6 Kit。
3. 配置 Debug 或 Release 构建目录。
4. 构建并运行 `modernFFplay`。

### 使用命令行

```bash
cmake -S . -B build
cmake --build build --config Release
```

Windows 下构建完成后，CMake 会把 `Lib/ffmpeg/bin` 中的 DLL 复制到可执行文件目录。

## 演进路线

| 阶段 | 状态 | 目标 |
| --- | --- | --- |
| Phase 0 | 已完成 | 基础播放器和初始分层整理 |
| Phase 1.1 | 已完成 | 播放控制、Seek、Stop、进度信号 |
| Phase 1.2 | 已完成 | 文件播放列表管理 |
| Phase 1.3 | 已完成 | 音量和静音控制 |
| Phase 1.4 | 已完成 | UI 交互改进、倍速 UI、全屏能力 |
| Phase 2 | 待启动 | 摄像头接入，优先普通 USB 摄像头，后续再考虑工业相机 |
| Phase 3.0 | 已完成 | 无音频流视频支持 |
| Phase 3.1 | 已完成 | 单路 `MediaPipeline` 抽取 |
| Phase 3.2 | 进行中 | `MultiStreamManager` 多路管理 |
| Phase 4 | 待启动 | RTSP/RTMP/网络流 |
| Phase 5+ | 待启动 | 图像处理、检测、测量和分析 |

## 文档索引

- [架构指导](doc/架构指导.md)
- [当前 TODO](doc/TODO.md)
- [Phase 3.1 单路 MediaPipeline 抽取计划](doc/Phase3_1_单路MediaPipeline抽取计划.md)
- [Phase 3.1 单路 MediaPipeline 抽取实现报告](doc/Phase3_1_单路MediaPipeline抽取实现报告.md)
- [Phase 3.2 多路管理架构预研报告](doc/Phase3_2_多路管理架构预研报告.md)
- [Phase 3.2 多路管理实施计划](doc/Phase3_2_多路管理实施计划.md)

## 技术栈

- Qt 6：窗口、控件、信号槽、线程和 OpenGL Widget。
- FFmpeg：封装格式解析、音视频解码、时间基处理和格式转换。
- OpenGL：视频帧渲染。
- CMake：项目构建。
- C++17：核心实现语言。

## 开发原则

- 严格遵循 `doc/架构指导.md`。
- UI 层不直接接触解复用、解码、队列和渲染细节。
- 应用层负责状态编排，不承载底层播放资源。
- 领域层保持业务边界清晰。
- 基础设施层只处理具体技术实现。
- 媒体源扩展通过 `IMediaSource` 完成。
- 多路管理通过 `MultiStreamManager` 扩展，不污染单路 `MediaPipeline`。

## 当前重点

下一步按 `doc/TODO.md` 推进 Phase 3.2：

1. 新增 `MultiStreamManager` 空壳。
2. 让 Manager 管理多个 `MediaPipeline`。
3. 转发带 `StreamId` 的管线信号。
4. 增加指定流和全局流控制能力。
5. 接入 `MediaPlayController`，保持现有单路 UI 行为不变。
6. 用本地文件源完成最小多路生命周期验证。
