# Phase 1.1 任务 4: decoder Flush 报告

## 结论

`decoder` 已新增统一 `Flush()` 能力，用于清理解码器内部缓存，避免 Seek 后继续输出 seek 前的缓存帧。

本任务只提供 decoder 层 flush 原语，不主动改变当前播放流程。实际 Seek 时何时调用 `Adecoder_->Flush()` 和 `Vdecoder_->Flush()`，由任务 5 的 `MediaPlayController` 统一编排。

## 代码改动

### decoder.h

新增公开接口：

```cpp
int Flush();
```

该接口放在 `StartDecoder()` 后，与播放控制能力同层级，供应用层 Controller 在 Seek/Stop 编排中调用。

### decoder.cpp

新增实现：

```cpp
int decoder::Flush() {
    if (!Ctx) {
        qWarning() << decoder_type << "decoder flush failed: context is null";
        return -1;
    }

    avcodec_flush_buffers(Ctx);

    if (frame) {
        av_frame_unref(frame);
    }

    qDebug() << decoder_type << "decoder flushed";
    return 0;
}
```

## Flush 职责

`Flush()` 负责：

1. 检查 `AVCodecContext* Ctx` 是否有效。
2. 调用 `avcodec_flush_buffers(Ctx)` 清空 FFmpeg 解码器内部缓存。
3. 如果当前复用帧 `frame` 已分配，则调用 `av_frame_unref(frame)` 清理引用。
4. 返回明确状态：成功 `0`，失败 `-1`。

## 硬解路径说明

当前硬解和软解共用同一个 `AVCodecContext`：

- 视频硬解：`Ctx->hw_device_ctx` 绑定硬件上下文，解码后可能走 `TransferHWFrame()`。
- 视频软解：直接从 `Ctx` receive frame。
- 音频软解：同样从 `Ctx` receive frame。

因此 flush 不需要区分硬解和软解，统一调用 `avcodec_flush_buffers(Ctx)` 即可。

硬解相关资源如 `m_hwDeviceCtx`、`m_swsCtx` 不应在 `Flush()` 中释放。它们属于 decoder 生命周期资源，释放它们会导致 seek 后无法继续复用当前 decoder。

## 当前限制

完成任务 4 后，`decoder` 已具备 flush 能力，但当前播放链路还不会自动调用它。

真实 Seek 仍需要任务 5 完成以下编排：

1. 队列 `RequestStop()`。
2. 队列 `ClearAndWake()`。
3. 音频/视频 decoder `Flush()`。
4. demuxer `SeekTo()`。
5. syncmanager 时钟重置。
6. 队列 `Resume()`。
7. 恢复播放或保持暂停。

## 验收结果

- 音频 decoder 和视频 decoder 走同一个 `Flush()` 接口。
- `Flush()` 内部调用了 `avcodec_flush_buffers()`。
- `Flush()` 不释放硬解生命周期资源，硬解路径可继续复用。
- `Flush()` 对空 `Ctx` 返回失败，不静默吞掉错误。
