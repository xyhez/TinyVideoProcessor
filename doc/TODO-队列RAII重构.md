# TODO: 队列 RAII 重构

## 当前进度

- 已完成：
  - 当前项目已有统一的 `SyncQueue<T>`，用于音视频包队列和帧队列。
  - 当前队列支持阻塞 `Push()`、阻塞 `Pop()`、`TryPop()`、`Front()`、`Clear()`、停止和等待中断。
  - 已新增 `AVPacketPtr` / `AVFramePtr`，通过 `unique_ptr + FFmpeg 自定义 deleter` 表达单一所有权。
  - 音视频 packet/frame 队列已迁移为 `SyncQueue<AVPacketPtr>` / `SyncQueue<AVFramePtr>`。
  - `MediaPipeline -> VideoFrameScheduler -> IVideoFrameSink` 的帧所有权转移语义已改为 RAII 移动语义。
  - `SyncQueue` 已删除旧的手动 `deleter_` 路径，只负责同步队列和元素移动。
- 未完成：
  - 本轮未执行编译和运行验证，需要后续人工覆盖普通文件、无音频文件、多路文件和摄像头源。
  - `AudioPlayer::m_frame` 仍是内部临时裸指针资源，后续如需要可单独收口。
- 已知风险：
  - 直接把 `std::deque<T>` 改成 `std::deque<std::shared_ptr<T>>` 不符合当前单一所有权转移语义。
  - `AVPacket` / `AVFrame` 不能用普通 `delete` 释放，必须使用 `av_packet_free()` / `av_frame_free()`。
  - `SyncQueue` 删除 `deleter_` 后，不应再承载需要手动释放的裸指针资源。
- 本轮范围：
  - 收口队列 RAII 所有权模型。
  - 删除旧的 `SyncQueue` 手动 deleter 路径。
  - 形成验收报告并记录剩余风险。

## 阶段目标

将当前依赖裸指针和手动 deleter 的队列，逐步演进为基于 RAII 所有权的同步队列，让 `AVPacket` / `AVFrame` 的释放责任由类型系统表达，而不是依赖调用方记忆。

推荐目标方向：

```cpp
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

SyncQueue<AVPacketPtr>
SyncQueue<AVFramePtr>
```

该方向表达的是单一所有权转移：

```text
生产者创建资源
  -> Push(move)
    -> 队列持有
      -> Pop(move)
        -> 消费者持有
          -> 离开作用域自动释放
```

## 架构边界

- 不使用 `std::shared_ptr<AVFrame>` 作为默认方案。
- 不把 FFmpeg 释放逻辑散落在 `demuxer`、`decoder`、`scheduler` 和 renderer 中。
- 不让 `SyncQueue` 直接依赖具体业务模块。
- 不让 `VideoFrameScheduler` 访问 `MultiViewRenderer` 或多路布局信息。
- 不改变 `MediaPipeline` 单路职责。
- 不改变 `MultiStreamManager` 的 `StreamId` 管理边界。
- 不通过一次性替换破坏当前稳定播放链路。

## 参考文档

- `doc/架构指导.md`
- `doc/TODO.md`
- `doc/Phase3_7_帧所有权与清理协议.md`
- `doc/Phase3_7_视频帧调度边界设计.md`
- `doc/Phase3_10_VideoFrameScheduler抽取方案.md`
- `doc/Phase3_10_VideoFrameScheduler抽取验收报告.md`
- `src/core/queue.h`
- `src/domain/MediaPipeline.h`
- `src/infrastructure/demuxer.h`
- `src/infrastructure/decoder.h`
- `src/infrastructure/videoframescheduler.h`

## 1.1 队列所有权链路复盘

**目标**：先确认当前每一种队列元素从创建、入队、出队到释放的完整路径，不直接改代码。

**预计修改文件**：

- `doc/TODO-队列RAII重构.md`
- `doc/队列RAII重构_所有权链路报告.md`

**任务清单**：

- [x] 梳理 `AVPacket*` 的创建位置。
- [x] 梳理 `AVPacket*` 的入队位置。
- [x] 梳理 `AVPacket*` 的出队位置。
- [x] 梳理 `AVPacket*` 的最终释放位置。
- [x] 梳理 `AVFrame*` 的创建位置。
- [x] 梳理 `AVFrame*` 的入队位置。
- [x] 梳理 `AVFrame*` 的出队位置。
- [x] 梳理 `AVFrame*` 的最终释放位置。
- [x] 标记 `Clear()`、`Stop()`、`Seek()`、`Shutdown()` 中队列资源释放路径。
- [x] 标记当前是否存在裸指针重复释放或释放责任不明确的位置。

**验收标准**：

- [x] 能说明 packet 队列元素当前谁创建、谁释放。
- [x] 能说明 frame 队列元素当前谁创建、谁释放。
- [x] 能说明队列清空、seek、stop、shutdown 时残留资源如何释放。
- [x] 能判断哪些调用点适合先迁移，哪些调用点必须后迁移。

**测试流程**：

- [x] 本任务为链路复盘，不运行程序。
- [x] 静态检查：`git diff --check -- doc\TODO-队列RAII重构.md doc\队列RAII重构_所有权链路报告.md`
- [x] 定向搜索：`rg -n "AVPacket\\*|AVFrame\\*|SyncQueue|av_packet_alloc|av_packet_free|av_frame_alloc|av_frame_free|Clear\\(|Pop\\(|Push\\(" src`

**Commit message**：

```text
docs: 梳理队列资源所有权链路

- 新增队列 RAII 重构所有权链路报告
- 梳理 AVPacket 和 AVFrame 的创建、入队、出队和释放路径
- 记录 seek、stop、shutdown 中队列残留资源清理方式
- 明确后续队列 RAII 化的迁移顺序和风险点
```

## 1.2 FFmpeg RAII 类型设计

**目标**：设计 `AVPacket` / `AVFrame` 的 RAII 包装类型和自定义 deleter，不接入现有播放链路。

**预计修改文件**：

- `src/core/ffmpeg_raii.h`
- `doc/TODO-队列RAII重构.md`

**任务清单**：

- [x] 新增 `AVPacketDeleter`。
- [x] 新增 `AVFrameDeleter`。
- [x] 定义 `AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>`。
- [x] 定义 `AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>`。
- [x] 提供 `MakeAVPacket()` 工具函数。
- [x] 提供 `MakeAVFrame()` 工具函数。
- [x] 提供必要的 `GetRaw()` 或 `.get()` 使用说明。
- [x] 明确 RAII 类型只表达单一所有权，不用于共享显示帧。

**验收标准**：

- [x] RAII 类型能表达 FFmpeg 资源释放方式。
- [x] RAII 类型不依赖 UI、播放管线或渲染器。
- [x] RAII 类型可以作为后续 `SyncQueue<T>` 元素类型。
- [x] 不改变现有播放行为。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\core\ffmpeg_raii.h doc\TODO-队列RAII重构.md`
- [x] 定向搜索：`rg -n "AVPacketDeleter|AVFrameDeleter|AVPacketPtr|AVFramePtr|MakeAVPacket|MakeAVFrame" src\core doc\TODO-队列RAII重构.md`
- [ ] 人工编译建议：确认新增头文件语法正确。

**Commit message**：

```text
feat: 增加 FFmpeg RAII 资源类型

- 新增 AVPacket 和 AVFrame 的自定义 deleter
- 定义 AVPacketPtr 和 AVFramePtr 所有权类型
- 提供 packet/frame 创建辅助函数
- 为队列元素从裸指针迁移到 RAII 所有权做准备
```

## 1.3 SyncQueue 支持 move-only 元素

**目标**：让 `SyncQueue<T>` 可以安全承载 `std::unique_ptr` 这类 move-only 类型，同时保持现有裸指针队列暂时可用。

**预计修改文件**：

- `src/core/queue.h`
- `doc/TODO-队列RAII重构.md`

**任务清单**：

- [x] 将 `Push(T item)` 内部改为 move 入队。
- [x] 将 `Pop(T& item)` 改为 move 出队。
- [x] 将 `TryPop(T& item)` 改为 move 出队。
- [x] 评估 `Front(T& item)` 对 move-only 类型是否仍成立。
- [x] 为 move-only 类型新增 `FrontRaw()`、`Peek()` 或 `VisitFront()` 等只读观察接口。
- [x] 保持现有 `SyncQueue<AVPacket*>` / `SyncQueue<AVFrame*>` 调用点暂不退化。
- [x] 明确 `deleter_` 在 RAII 队列中应逐步废弃。
- [x] 不在本步迁移所有调用点。

**验收标准**：

- [x] `SyncQueue<T>` 仍兼容当前裸指针队列。
- [x] `SyncQueue<std::unique_ptr<...>>` 的接口设计可成立。
- [x] 队列清空时 move-only 元素能自动释放。
- [x] `Front()` 替代方案能满足 `VideoFrameScheduler` 的队首帧同步判断需求。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\core\queue.h doc\TODO-队列RAII重构.md`
- [x] 定向搜索：`rg -n "Push\\(|Pop\\(|TryPop\\(|Front\\(|VisitFront|FrontRaw|std::move|deleter_" src\core\queue.h doc\TODO-队列RAII重构.md`
- [ ] 人工编译建议：确认当前裸指针队列调用不退化。

**Commit message**：

```text
refactor: 让 SyncQueue 支持 move-only 元素

- 调整 SyncQueue 入队和出队逻辑以支持移动语义
- 保持现有裸指针队列调用兼容
- 设计队首元素只读观察接口替代直接复制 Front
- 为 AVPacketPtr 和 AVFramePtr 队列迁移打基础
```

## 1.4 Packet 队列 RAII 迁移

**目标**：优先迁移 `AVPacket` 队列，因为 packet 生命周期相对短，链路比 frame 显示所有权更简单。

**预计修改文件**：

- `src/core/ffmpeg_raii.h`
- `src/core/queue.h`
- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/infrastructure/demuxer.h`
- `src/infrastructure/demuxer.cpp`
- `src/infrastructure/decoder.h`
- `src/infrastructure/decoder.cpp`
- `doc/TODO-队列RAII重构.md`

**任务清单**：

- [x] 将音频包队列类型从 `SyncQueue<AVPacket*>` 迁移为 `SyncQueue<AVPacketPtr>`。
- [x] 将视频包队列类型从 `SyncQueue<AVPacket*>` 迁移为 `SyncQueue<AVPacketPtr>`。
- [x] `demuxer` 读取 packet 后以 move 方式入队。
- [x] `decoder` 以 move 方式出队并使用 `.get()` 调用 FFmpeg 解码接口。
- [x] `Push()` 失败时不再手动调用 `av_packet_free()`。
- [x] `Clear()` 时由 `AVPacketPtr` 自动释放资源。
- [x] 保持 stop / seek / shutdown 的 packet 清理语义不变。

**验收标准**：

- [x] packet 队列不再需要外部 deleter。
- [x] packet 出队后所有权明确归 decoder。
- [x] packet 队列清空时没有泄漏。
- [ ] 普通文件播放不退化。
- [ ] seek、stop、shutdown 不退化。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\core\ffmpeg_raii.h src\core\queue.h src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp src\infrastructure\demuxer.h src\infrastructure\demuxer.cpp src\infrastructure\decoder.h src\infrastructure\decoder.cpp doc\TODO-队列RAII重构.md`
- [x] 定向搜索：`rg -n "SyncQueue<AVPacket\\*>|SyncQueue<AVPacketPtr>|av_packet_free|AVPacketPtr|Push\\(|Pop\\(" src doc\TODO-队列RAII重构.md`
- [ ] 人工运行建议：普通 MP4 播放、seek、stop、关闭程序。

**Commit message**：

```text
refactor: 将 packet 队列迁移到 RAII 所有权

- 使用 AVPacketPtr 表达 packet 单一所有权
- 将音频和视频 packet 队列迁移为 move-only 队列
- 让 demuxer 入队后转移 packet 所有权
- 让 decoder 出队后临时持有 packet 所有权
- 移除 packet 队列对手动 deleter 的依赖
```

## 1.5 Frame 队列 RAII 迁移方案

**目标**：先设计 frame 队列迁移方案，重点解决 `VideoFrameScheduler::Front()` 队首观察语义和 `IVideoFrameSink` 所有权转移语义。

**预计修改文件**：

- `doc/队列RAII重构_Frame迁移方案.md`
- `doc/TODO-队列RAII重构.md`

**任务清单**：

- [x] 分析 `VideoFrameScheduler` 当前 `Front()`、`Pop()`、`DropLateFrame()` 语义。
- [x] 明确 `AVFramePtr` 队首观察方案。
- [x] 明确可显示帧从 scheduler 转交给 sink 时是否继续使用 raw pointer 接口。
- [x] 评估是否将 `IVideoFrameSink::PresentFrame(AVFrame*)` 改为 `PresentFrame(AVFramePtr)`。
- [x] 评估 `VideoRender` 和 `MultiViewRenderer` 当前显示帧持有方式。
- [x] 明确是否先在 scheduler 内部 `.release()` 兼容旧 sink 接口。
- [x] 给出最小风险迁移顺序。

**验收标准**：

- [x] 能说明 `Front()` 在 move-only frame 队列下如何替代。
- [x] 能说明 `AVFramePtr` 如何从 decoder 转移到 scheduler。
- [x] 能说明可显示帧如何转移到 sink。
- [x] 能说明旧 `VideoRender` fallback 是否需要同步迁移。

**测试流程**：

- [x] 本任务为方案文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\队列RAII重构_Frame迁移方案.md doc\TODO-队列RAII重构.md`
- [x] 定向搜索：`rg -n "Front\\(|PopDisplayableFrame|DropLateFrame|PresentFrame|AVFrame\\*|m_displayFrame|currentFrame" src`

**Commit message**：

```text
docs: 设计 frame 队列 RAII 迁移方案

- 分析 VideoFrameScheduler 的队首观察和出队语义
- 设计 AVFramePtr 队列下的同步判断方式
- 明确可显示帧到 IVideoFrameSink 的所有权转移方案
- 规划 VideoRender 和 MultiViewRenderer 的显示帧持有方式
```

## 1.6 Frame 队列 RAII 迁移实现

**目标**：在方案确认后，将音频帧队列和视频帧队列迁移到 `AVFramePtr`。

**预计修改文件**：

- `src/core/ffmpeg_raii.h`
- `src/core/queue.h`
- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/infrastructure/decoder.h`
- `src/infrastructure/decoder.cpp`
- `src/infrastructure/audioplayer.h`
- `src/infrastructure/audioplayer.cpp`
- `src/infrastructure/videoframescheduler.h`
- `src/infrastructure/videoframescheduler.cpp`
- `src/infrastructure/ivideoframesink.h`
- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`
- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `src/infrastructure/singlestreamframesinkadapter.h`
- `src/infrastructure/singlestreamframesinkadapter.cpp`
- `doc/TODO-队列RAII重构.md`

**任务清单**：

- [x] 将音频帧队列迁移为 `SyncQueue<AVFramePtr>`。
- [x] 将视频帧队列迁移为 `SyncQueue<AVFramePtr>`。
- [x] `decoder` 解码输出后以 move 方式入队。
- [x] `AudioPlayer` 出队后持有 `AVFramePtr`，处理完自动释放。
- [x] `VideoFrameScheduler` 支持队首观察和 move 出队。
- [x] `DropLateFrame()` 丢弃帧时依赖 RAII 自动释放。
- [x] `PresentDisplayableFrame()` 明确帧所有权转移。
- [x] `VideoRender` / `MultiViewRenderer` 显示帧持有方式与新所有权语义一致。
- [x] seek、stop、remove stream、shutdown 时不重复释放帧。

**验收标准**：

- [x] frame 队列不再需要外部 deleter。
- [x] decoder 输出帧后所有权明确进入队列。
- [x] scheduler 显示帧时所有权明确转移到 sink。
- [x] drop late frame 自动释放。
- [ ] stop / seek / shutdown 不泄漏、不重复释放、不显示旧帧。
- [ ] 多路文件播放不退化。
- [ ] 无音频流播放不退化。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\core\ffmpeg_raii.h src\core\queue.h src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp src\infrastructure\decoder.h src\infrastructure\decoder.cpp src\infrastructure\audioplayer.h src\infrastructure\audioplayer.cpp src\infrastructure\videoframescheduler.h src\infrastructure\videoframescheduler.cpp src\infrastructure\ivideoframesink.h src\infrastructure\videorender.h src\infrastructure\videorender.cpp src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\infrastructure\singlestreamframesinkadapter.h src\infrastructure\singlestreamframesinkadapter.cpp doc\TODO-队列RAII重构.md`
- [x] 定向搜索：`rg -n "SyncQueue<AVFrame\\*>|SyncQueue<AVFramePtr>|av_frame_free|AVFramePtr|PresentFrame|DropLateFrame|ClearFrame" src doc\TODO-队列RAII重构.md`
- [ ] 人工运行建议：普通 MP4 播放、seek、pause、stop、播放中关闭。
- [ ] 人工运行建议：无音频流视频播放、seek、关闭。
- [ ] 人工运行建议：多路 2x2 播放、切 active stream、remove stream、关闭。

**Commit message**：

```text
refactor: 将 frame 队列迁移到 RAII 所有权

- 使用 AVFramePtr 表达视频帧和音频帧单一所有权
- 将音频帧队列和视频帧队列迁移为 move-only 队列
- 调整 VideoFrameScheduler 的队首观察、丢帧和显示帧转移逻辑
- 让 AudioPlayer、VideoRender 和 MultiViewRenderer 遵循新的帧所有权语义
- 移除 frame 队列对手动 deleter 的依赖
```

## 1.7 删除旧 deleter 路径与收口报告

**目标**：在 packet/frame 队列都迁移完成后，删除 `SyncQueue` 中旧的手动 deleter 路径，并形成收口报告。

**预计修改文件**：

- `src/core/queue.h`
- `doc/队列RAII重构_验收报告.md`
- `doc/TODO-队列RAII重构.md`

**任务清单**：

- [x] 删除或废弃 `SyncQueue` 构造函数中的 `std::function<void(T)> deleter`。
- [x] 删除 `Clear()` 中对 `deleter_` 的调用。
- [x] 删除 `Push()` 失败时对 `deleter_` 的调用。
- [x] 确认 `SyncQueue` 只负责同步队列，不再负责具体资源释放。
- [x] 确认所有 FFmpeg packet/frame 队列均使用 RAII 元素。
- [x] 记录最终所有权模型。
- [x] 记录已验证场景和剩余风险。

**验收标准**：

- [x] `SyncQueue` 不再感知 FFmpeg 资源释放函数。
- [x] `AVPacket` / `AVFrame` 资源释放由 RAII 类型负责。
- [ ] 当前播放、seek、stop、shutdown、多路、无音频流场景行为不退化。
- [x] 文档能说明为什么选择 `unique_ptr` 而不是 `shared_ptr`。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\core\queue.h doc\队列RAII重构_验收报告.md doc\TODO-队列RAII重构.md`
- [x] 定向搜索：`rg -n "deleter_|std::function<void\\(T\\)>|SyncQueue<AVPacket\\*>|SyncQueue<AVFrame\\*>|av_packet_free|av_frame_free" src doc\TODO-队列RAII重构.md`
- [ ] 人工运行建议：普通文件、无音频文件、多路文件、摄像头源。

**Commit message**：

```text
refactor: 收口 SyncQueue RAII 所有权模型

- 删除 SyncQueue 中旧的手动 deleter 路径
- 让队列只负责同步和元素移动，不负责具体资源释放
- 确认 AVPacket 和 AVFrame 生命周期由 RAII 类型管理
- 新增队列 RAII 重构验收报告
- 记录 unique_ptr 所有权模型和验证场景
```

## 不做范围

- 不直接使用 `std::shared_ptr<AVPacket>` / `std::shared_ptr<AVFrame>` 作为默认队列元素。
- 不在本重构中修改音视频同步算法。
- 不在本重构中修改多路布局或 `MultiViewRenderer` 绘制逻辑。
- 不在本重构中处理非 `YUV420P`、4K MKV 色彩或性能优化问题。
- 不引入第三方内存管理库。
- 不一次性删除 `VideoRender` fallback。

## 风险与沟通点

- `AVFramePtr` 迁移比 `AVPacketPtr` 风险更高，因为视频帧会穿过 `VideoFrameScheduler`、`IVideoFrameSink`、`VideoRender` 和 `MultiViewRenderer`。
- `Front()` 复制队首元素的旧接口不适合 move-only 类型，必须先设计新的队首观察接口。
- 如果 `IVideoFrameSink` 从 `AVFrame*` 改成 `AVFramePtr`，会影响所有 sink 实现，应单独确认后再改。
- 如果为了兼容旧接口临时 `.release()`，必须明确后续谁负责接管并释放，不能形成新的裸指针泄漏点。
- 本重构应先迁移 packet 队列，再迁移 frame 队列，避免一次性触碰整条音视频链路。
