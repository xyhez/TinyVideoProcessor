# Phase 1.1 任务 3: demuxer 真实 Seek 报告

## 结论

`demuxer` 已具备真实 Seek 原语：

- 新增 `SeekTo(double seconds)`，负责跳到绝对时间。
- 新增 `SeekRelative(double seconds)`，负责基于当前读取位置相对跳转。
- `Rewind()` / `Forward()` 已改为调用 `SeekRelative(-10.0)` / `SeekRelative(10.0)`。
- `SeekTo()` 内部使用 `av_seek_frame()` 跳转。
- `SeekTo()` 会处理小于 0 和超过总时长的边界。
- Seek 成功后会清理 demuxer 内部当前音视频 PTS 记录。

本任务只完成 demuxer 层能力，不代表完整播放器 Seek 已完成。后续仍需要任务 4 的 decoder flush，以及任务 5 的 Controller 编排。

## 代码改动

### demuxer.h

新增公开接口：

```cpp
int SeekTo(double seconds);
int SeekRelative(double seconds);
```

新增私有工具方法：

```cpp
double CurrentPositionSeconds() const;
```

同时将 `mutex` 改为 `mutable QMutex`，让 `CurrentPositionSeconds() const` 可以安全读取当前 PTS。

当前 PTS 类型从 `int` 调整为 `int64_t`，避免 FFmpeg PTS 超过 32 位范围。

### demuxer.cpp

新增 `QMutexLocker` 使用。

`StartDemuxer()` 中，`av_read_frame()` 现在被 `mutex` 保护，避免它和 `SeekTo()` 中的 `av_seek_frame()` 并发访问同一个 `AVFormatContext`。

锁粒度保持较小：

- 加锁：检查 stop flag、调用 `av_read_frame()`、更新当前 PTS。
- 不加锁：向音频/视频包队列 `Push()`。

这样可以避免队列满导致 `Push()` 阻塞时把 seek 锁长期占住。

## SeekTo 职责

`SeekTo(double seconds)` 负责底层真实跳转：

1. 检查媒体源是否存在。
2. 通过 `IMediaSource::GetFormatContect()` 获取 `AVFormatContext`。
3. 将目标时间限制到合法区间。
4. 将秒转换为 FFmpeg 全局时间基：`seconds * AV_TIME_BASE`。
5. 调用：

```cpp
av_seek_frame(ic, -1, targetTs, AVSEEK_FLAG_BACKWARD);
```

6. seek 失败时打印 FFmpeg 错误并返回 `-1`。
7. seek 成功后将 `m_AcurrentPTS` / `m_VcurrentPTS` 清零。

这里使用 `stream_index = -1`，目标时间戳单位为 `AV_TIME_BASE`，适合作为当前阶段的通用实现。

## SeekRelative 职责

`SeekRelative(double seconds)` 不直接调用 FFmpeg。

它只负责：

1. 读取当前 demuxer 位置。
2. 计算目标时间：`CurrentPositionSeconds() + seconds`。
3. 调用 `SeekTo(target)`。

所以：

```cpp
Rewind()  -> SeekRelative(-10.0)
Forward() -> SeekRelative(10.0)
```

后续 UI 进度条拖动应该调用 `SeekTo(targetSeconds)`，键盘快进快退则调用 `SeekRelative(deltaSeconds)`。

## 当前限制

此任务完成后，左右键可以触发 demuxer 层真实 `av_seek_frame()`，但播放效果仍可能不稳定，因为完整 Seek 链路还缺：

- 清空旧音频包/视频包/音频帧/视频帧。
- flush 音频 decoder 和视频 decoder。
- 重置 `syncmanager` 主时钟。
- 清理音频输出侧旧 PCM。
- 清理或刷新当前显示帧。
- 根据 seek 前状态恢复播放或保持暂停。

这些不应放进 demuxer。demuxer 只负责媒体读取位置变化，完整流程应该由 `MediaPlayController` 在任务 5 统一编排。

## 验收结果

- `demuxer::SeekTo(double seconds)` 已存在。
- `demuxer::SeekRelative(double seconds)` 已存在。
- `Rewind()` / `Forward()` 已接入相对 Seek。
- `SeekTo()` 已处理目标时间上下界。
- `SeekTo()` 失败时返回 `-1`，不会静默吞掉错误。
- `av_read_frame()` 和 `av_seek_frame()` 已使用同一把 mutex 保护，避免并发访问 `AVFormatContext`。
