# Phase 0: 架构重构计划

## Context

当前项目是一个 Qt6 + FFmpeg + OpenGL 视频播放器，功能可运行但架构存在严重问题：
- MainWindow 承担所有职责（UI + 业务逻辑 + 资源管理），代码 270 行且边界模糊
- 全局变量（`global.h/cpp`）管理队列和同步原语，各组件硬编码依赖全局状态
- 裸指针与 shared_ptr 混用，资源管理不一致
- 组件间直接持有指针调用，无抽象层，无法扩展
- 存在多个已知 bug（锁不一致、static SwsContext 线程不安全、getClock 无锁保护等）

Phase 0 目标：在**不改变播放功能**的前提下，重构架构为可扩展的分层设计。

## 重构策略

**核心原则**：渐进式重构，每步可编译、可运行、可测试。不一次性大改，而是逐步迁移。

### 重构步骤（共 5 步）

---

### Step 1: 封装队列管理 → `SyncQueue` 模板类

**动机**：当前 8 个全局同步原语（4 mutex + 4 cv）暴露在 `global.h`，各组件自行 lock/unlock，容易出错且代码重复。

**新文件**：
- `src/core/queue.h` — 模板类 `SyncQueue<T>`，封装 `deque + mutex + cv`，提供 push/pop/tryPop/clear/wakeAll

**改动**：
- `demuxer`、`decoder`、`AudioPlayer`、`VideoRender` 改用 `SyncQueue` 方法
- 删除 `global.h/cpp` 文件

**验证**：编译通过 + 原播放功能正常

---

### Step 2: 引入 `IMediaSource` 抽象 + 修复 `syncmanager`

**动机**：当前 `demuxer::initDemuxer(QString filename)` 硬编码文件路径，无法扩展到摄像头/网络流。

**新文件**：
- `src/domain/IMediaSource.h` — 纯虚接口
- `src/domain/FileMediaSource.h/.cpp` — 文件媒体源实现（封装 `AVFormatContext` 打开/关闭/获取流信息）

**改动**：
- `demuxer` 改为接收 `IMediaSource`，不再直接持有文件名
- `syncmanager` 修复 `getClock()` 线程安全问题（`mutable QMutex`）

**验证**：编译通过 + 原播放功能正常 + 可切换不同文件

---

### Step 3: 提取 `MediaPlayerController`

**动机**：MainWindow 中的 `start()/pause()/stop()/keyReleaseEvent` 逻辑应该属于应用层控制器。

**新文件**：
- `src/application/MediaPlayerController.h/.cpp` — 管理播放生命周期，协调各组件的启动、暂停、停止

**改动**：
- 从 MainWindow 迁移：线程创建、组件初始化、start/pause/stop 逻辑
- MainWindow 只保留：UI 按钮/键盘事件 → 转发给 Controller
- 信号槽连接迁移到 Controller

**验证**：编译通过 + 原播放功能正常 + MainWindow 行数 < 150

---

### Step 4: 统一智能指针管理资源

**动机**：当前 decoder*、demuxer*、AudioPlayer* 是裸指针+手动 delete。

**改动**：
- `demuxer`、`decoder`、`AudioPlayer` 改为 `std::unique_ptr` 持有
- `VideoRender` 保持 Qt parent 管理（QWidget 约定）
- 移除 `freeRsources()/freeVec()/freePacketVec()/freeFrameVec()` 方法
- 修复 `freePacketVec()` 中的错误锁

**验证**：编译通过 + 关闭窗口无内存泄漏 + 原播放功能正常

---

### Step 5: 修复已知 Bug + 组件间信号槽解耦

**动机**：修复发现的问题，同时将直接调用改为信号槽。

**改动**：
- `decoder::transferHWFrame()` — 将 `static SwsContext` 改为成员变量，修复线程安全
- `syncmanager::getClock()` — 加 `mutable QMutex` 并 lock
- `demuxer` 视频包队列 — 恢复 backpressure 限制
- 播放状态变更改用信号：`Controller::stateChanged(PlayState)` → 各组件响应

**验证**：编译通过 + 原播放功能正常 + 多线程安全

---

## 目录结构（重构后）

```
modernFFplay/
├── src/
│   ├── core/
│   │   └── queue.h                  # SyncQueue 模板
│   ├── domain/
│   │   ├── IMediaSource.h           # 媒体源抽象接口
│   │   └── FileMediaSource.h/.cpp   # 文件媒体源实现
│   ├── application/
│   │   └── MediaPlayerController.h/.cpp  # 播放控制器
│   ├── infrastructure/
│   │   ├── demuxer.h/.cpp
│   │   ├── decoder.h/.cpp
│   │   ├── audioplayer.h/.cpp
│   │   ├── videorender.h/.cpp
│   │   └── syncmanager.h/.cpp
│   └── ui/
│       ├── mainwindow.h/.cpp/.ui
│       └── main.cpp
├── CMakeLists.txt                    # 更新，添加新目录
├── .gitignore
└── 架构指导.md
```

## 不改变的部分

- 播放功能流程不变（demux → decode → render/play）
- 音视频同步算法不变
- OpenGL 渲染管线不变
- UI 布局不变

## 验证计划

1. **编译**：`cmake --build build` 零警告零错误
2. **播放测试**：启动后播放测试视频，验证：
   - 画面正常渲染
   - 音频正常播放
   - 空格键暂停/恢复
   - 上下方向键音量调节
   - 左右方向键快进
3. **关闭测试**：关闭窗口无 crash，无内存泄漏
4. **MainWindow 行数**：< 150 行（当前 270 行）
