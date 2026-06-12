# Phase 1.1 任务 2: 队列 Seek 能力报告

## 结论

`SyncQueue` 已具备释放队列内 AVPacket/AVFrame 的基础能力，但原接口语义不够完整：`Clear()` 只清空并唤醒生产者，没有唤醒等待消费者；调用方也需要手写 `SetStopFlag(true) + WakeAll()`，后续 Seek 编排中容易漏掉步骤。

本任务已补齐队列层的三个关键能力：

- `ClearAndWake()`：语义化表达“清空旧数据并唤醒等待者”。
- `RequestStop()`：语义化表达“停止队列并唤醒阻塞中的 Push/Pop”。
- `Resume()`：语义化表达“恢复队列可写可读状态并唤醒等待者”。

同时 `Clear()` 已改为同时唤醒 `cv_not_full_` 和 `cv_not_empty_`，避免消费者在清空后仍长期等待。

## 当前队列行为

### Push

`Push()` 在队列满时等待 `cv_not_full_`。

当 `stop_flag_ == true` 时：

- 不再入队。
- 如果存在 deleter，会立即释放传入 item。
- 直接返回。

这对 Stop/Seek 很重要：当 Seek 期间旧 demuxer 线程还试图塞旧包时，队列不会继续接收旧数据。

### Pop

`Pop()` 在队列空时等待 `cv_not_empty_`。

当队列为空且 `stop_flag_ == true` 时：

- 返回 false。
- 调用方可以退出当前消费循环。

这对 decoder/audio player 很重要：Seek 或 Stop 时可以打断阻塞中的消费者。

### Clear

`Clear()` 当前会：

- 持锁遍历队列。
- 对每个元素调用 deleter。
- 弹出所有元素。
- 唤醒 `cv_not_full_`。
- 唤醒 `cv_not_empty_`。

因为 `MediaPlayController` 创建队列时传入了：

```cpp
packetDeleter = av_packet_free
frameDeleter = av_frame_free
```

所以四个队列清空时可以正确释放：

- 音频 AVPacket
- 视频 AVPacket
- 音频 AVFrame
- 视频 AVFrame

## 本次代码改动

### 1. `Clear()` 同时唤醒消费者

原来只唤醒 `cv_not_full_`，适合释放生产者阻塞，但不适合 Seek 场景。Seek 时，消费者也可能正在等新数据，如果队列被清空但没有唤醒消费者，后续状态切换容易变得难判断。

现在 `Clear()` 同时唤醒：

- `cv_not_full_`
- `cv_not_empty_`

### 2. 新增 `ClearAndWake()`

目前实现直接调用 `Clear()`。

它的价值不是复杂逻辑，而是为 Controller 的 Seek 编排提供明确语义。后续代码读起来应该是：

```cpp
APacket_->ClearAndWake();
VPacket_->ClearAndWake();
AFrame_->ClearAndWake();
VFrame_->ClearAndWake();
```

而不是让读者猜 `Clear()` 是否负责唤醒。

### 3. 新增 `RequestStop()`

封装：

```cpp
SetStopFlag(true);
WakeAll();
```

已经同步替换当前 Stop/ReleaseResources 中的重复写法。

### 4. 新增 `Resume()`

封装：

```cpp
SetStopFlag(false);
WakeAll();
```

后续 Seek 完成后，Controller 可以恢复队列接收新数据。这个能力是当前 Stop/Seek 缺失的关键之一。

## Seek 时建议队列顺序

后续 Controller 的 Seek 编排建议使用固定顺序：

1. 记录 seek 前播放状态：Playing / Paused。
2. 暂停音频输出和视频渲染。
3. 四个队列 `RequestStop()`，打断阻塞中的 Push/Pop。
4. 四个队列 `ClearAndWake()`，释放所有旧包旧帧。
5. flush 音频 decoder 和视频 decoder。
6. demuxer 执行真实 `SeekTo(targetSeconds)`。
7. 同步时钟设置到目标时间。
8. 四个队列 `Resume()`，允许新包新帧进入。
9. 如果 seek 前是 Playing，则恢复播放；如果 seek 前是 Paused，则保持暂停。

## 死锁风险评估

### 清空时与 Push 并发

`Clear()` 和 `Push()` 使用同一个 mutex。

- 如果 `Push()` 正在入队，`Clear()` 会等待 Push 释放锁。
- 如果 `Clear()` 先拿到锁，`Push()` 会等待 Clear 完成。
- Clear 完成后唤醒 not_full，生产者不会因为队列满而永久等待。

结论：不会因为 Clear/Push 并发产生队列内部死锁。

### 清空时与 Pop 并发

`Clear()` 和 `Pop()` 使用同一个 mutex。

- 如果 `Pop()` 已取走元素，Clear 不会重复释放该元素。
- 如果 `Clear()` 先清空队列，Pop 后续会看到空队列。
- Clear 现在会唤醒 not_empty，等待中的 Pop 能重新检查条件。

注意：如果 `stop_flag_ == false` 且队列为空，Pop 被唤醒后仍会继续等待。这是 condition_variable 的正常语义。所以 Seek 时必须先 `RequestStop()`，再清空队列，才能让 Pop 返回 false 并退出旧消费路径。

## 验收结果

- Seek 时四个队列可以通过 `ClearAndWake()` 统一清空。
- 队列清空时会调用 deleter，AVPacket/AVFrame 不会滞留在队列内。
- 队列通过 `Resume()` 可以恢复 `stop_flag_ = false`，后续可以重新接收新数据。
- 当前 Stop/ReleaseResources 已开始使用 `RequestStop()`，减少重复手写停止逻辑。

## 后续任务衔接

任务 3 做 demuxer 真实 Seek 时，不需要再改队列底层能力。

但任务 5 在 Controller 编排 Seek 时，必须使用本任务新增的语义接口：

- Seek 前：`RequestStop()`
- 清旧数据：`ClearAndWake()`
- Seek 后：`Resume()`

如果只调用 `ClearAndWake()` 而不先 `RequestStop()`，阻塞中的 Pop 仍可能继续等待；如果 Seek 后忘记 `Resume()`，Push 会继续释放新包，播放无法恢复。
