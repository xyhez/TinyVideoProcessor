# Phase 1: 完善播放器基础功能计划

## 目标

在 Phase 0 分层架构稳定的基础上，完善本地视频播放器的基础功能，让播放器从“能播放”推进到“可正常使用”。

Phase 1 仍然遵循渐进式演进：每次只完成一个可验证的小功能，不打补丁式绕过架构边界。`MainWindow` 继续只负责 UI 交互，播放控制、媒体管线协调和状态流转仍由 `MediaPlayController` 承担。

## 当前基础

- `MainWindow` 已瘦身，播放逻辑已迁移到 `MediaPlayController`。
- `MediaPlayController` 已依赖 `IMediaSource` 抽象。
- 文件播放由 `FileMediaSource` 提供。
- 音视频同步已使用 `Fileinfo` 中的流级 `time_base`。
- 播放状态已由 `MediaPlayController::PlaybackState` 管理。

## 1.1 播放控制

### 目标

完善播放、暂停、停止、快进、快退、Seek 和播放速度控制，先保证键盘控制可用，再扩展到进度条 UI。

### 功能清单

- [x] 播放/暂停
- [x] 停止并回到起始位置
- [x] 快进/快退固定秒数
- [x] Seek 到指定时间点
- [x] 进度条拖动 Seek
- [x] 播放速度控制
- [x] 播放位置、总时长、状态信号

### 技术方案

Seek 的核心流程：

1. 暂停或阻塞当前读取、解码、渲染、播放流程。
2. 调用 `av_seek_frame()` 定位到目标时间。
3. 清空音频包队列、视频包队列、音频帧队列、视频帧队列。
4. flush 音视频解码器，避免旧帧继续输出。
5. 重置同步时钟到目标位置。
6. 恢复 demux/decode/render/play 流程。

播放控制接口建议：

```cpp
void Stop();
void SeekTo(double seconds);
void SeekRelative(double seconds);
void SetPlaybackSpeed(double speed);
double Position() const;
double Duration() const;
```

信号建议：

```cpp
void PositionChanged(double seconds);
void DurationChanged(double seconds);
void PlaybackSpeedChanged(double speed);
```

### 注意事项

- 不要直接从 `AVFrame::time_base` 推导播放时间；继续使用媒体源解析出的流级 `time_base`。
- Seek 后必须清空队列，否则旧包/旧帧会继续播放，表现为跳转无效或音视频错位。
- Seek 后必须 flush decoder，否则解码器内部缓存可能输出 seek 前的帧。
- Stop 和 Seek 可以共用“停止管线 + 清空队列 + 重置时钟”的内部工具方法，但不要把 Stop 写成只设置停止标志。

## 1.2 文件列表管理

### 目标

让播放器支持多个本地文件的管理和切换。

### 功能清单

- [x] 播放列表数据结构
- [x] 添加文件
- [x] 移除文件
- [x] 清空列表
- [x] 上一个/下一个
- [x] 单曲循环、列表循环、顺序播放、随机播放
- [ ] 最近播放记录（本阶段跳过）

### 架构建议

新增 `PlaylistManager`，由 UI 层触发文件选择，应用层或独立管理类保存播放列表。切换媒体时重新创建 `IMediaSource` 并交给 `MediaPlayController` 初始化。

## 1.3 音量控制

### 目标

把当前键盘音量增减扩展成完整音量能力。先完成音量滑块、静音/取消静音和状态信号，音量记忆暂不纳入本轮。

### 功能清单

- [x] 音量增减
- [x] 音量滑块
- [x] 静音/取消静音
- [x] 音量值信号
- [ ] 音量记忆（暂不做）

### 架构建议

`AudioPlayer` 负责实际音量应用，`MediaPlayController` 对外提供统一接口，`MainWindow` 只绑定 UI 控件和信号。

### 推荐拆分

1. 梳理当前音量链路。
2. 收敛 `AudioPlayer` 的 `SetVolume()` / `Volume()` / 静音能力。
3. 在 `MediaPlayController` 增加统一音量接口和状态信号。
4. 在 UI 接入音量滑块和静音按钮。
5. 做边界检查并形成报告。

## 1.4 UI 改进

### 目标

补齐本地播放器常见 UI 控件。

### 功能清单

- [x] 进度条
- [x] 当前时间/总时长
- [x] 音量滑块
- [x] 播放速度选择
- [x] 播放列表面板
- [x] 全屏模式
- [ ] 全屏时鼠标自动隐藏

### 当前剩余任务

1. 增加全屏时鼠标自动隐藏。
2. 完成 Phase 1.4 UI 总体验收报告。

### 注意事项

UI 改动必须继续保持 `MainWindow` 只做交互转发，不把播放管线细节拉回 UI 层。

## 1.5 验收标准

- [x] 播放、暂停、停止行为稳定。
- [x] 快进/快退后音视频同步。
- [x] Seek 精度小于 1 秒。
- [x] Seek 后无旧帧闪回、无旧音频残留。
- [x] 进度条显示与实际播放位置一致。
- [x] 音量和静音 UI 状态与实际输出一致。
- [x] `MainWindow` 仍不直接依赖基础设施层播放组件。
- [x] 不引入新的全局状态。

## 推荐推进顺序

1. 同步 Phase 1 文档状态。
2. 实现播放速度 UI 选择控件。
3. 实现全屏模式。
4. 实现全屏时鼠标自动隐藏。
5. 完成 Phase 1.4 UI 总体验收报告。
