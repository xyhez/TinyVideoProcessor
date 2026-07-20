# TODO-播放状态与资源安全修复

## 当前进度

- 已完成：根因静态复核与完成协议设计。
- 未完成：代码实现、回归测试和静态验收。
- 已知风险：Qt 音频设备 drain 与 DirectShow 中断需要实机验证。
- 本轮范围：队列、音频、解复用/解码、视频调度、摄像头媒体源和 MediaPipeline。

## Phase 1：队列与音频语义

- [ ] Phase 1.1：区分 Item、Interrupted、Stopped、EndOfStream。
  - 修改文件：`src/core/queue.h`、`tests/test_sync_queue.cpp`
  - 验收标准：EOS 先排空，Interrupt 不丢数据，Stop 立即取消，Resume 重置全部终止状态。
  - 测试流程：运行 `test_sync_queue`，覆盖四种结果和恢复路径。
  - Commit message：`fix: 区分同步队列结束与中断语义`

- [ ] Phase 1.2：修复 AudioPlayer 中断误退出并补自然收尾。
  - 修改文件：`src/infrastructure/audio/audioplayer.h`、`src/infrastructure/audio/audioplayer.cpp`
  - 验收标准：Seek 中断不退出工作循环；EOS 后发出音频排空信号。
  - 测试流程：空音频帧队列进入 Interrupt 后恢复；短 AAC 文件检查尾音。
  - Commit message：`fix: 修复音频队列中断竞态`

## Phase 2：Decoder 与 EOS 完成协议

- [ ] Phase 2.1：修复 Decoder 初始化和 scratch frame 生命周期。
  - 修改文件：`src/infrastructure/decoding/decoder.h`、`src/infrastructure/decoding/decoder.cpp`、`src/domain/MediaPipeline.cpp`
  - 验收标准：所有 FFmpeg 初始化错误均返回失败；无裸 `AVFrame` 泄漏。
  - 测试流程：无效 codec 参数初始化失败；内存工具反复创建/销毁 Decoder。
  - Commit message：`fix: 传播解码初始化错误并修复帧泄漏`

- [ ] Phase 2.2：建立 Demuxer -> Decoder -> Consumer EOS 链。
  - 修改文件：`src/infrastructure/decoding/demuxer.cpp`、`src/infrastructure/decoding/decoder.h`、`src/infrastructure/decoding/decoder.cpp`、`src/domain/MediaPipeline.h`、`src/domain/MediaPipeline.cpp`
  - 验收标准：Demux EOF 不立即停止帧队列；Decoder 完成 codec drain；Pipeline 等所需消费者排空后发完成信号。
  - 测试流程：含 B 帧视频和 AAC 音频验证最后 PTS 与完成信号顺序。
  - Commit message：`fix: 按消费链完成媒体 EOS drain`

## Phase 3：调度与摄像头关闭

- [ ] Phase 3.1：消除重复 Tick。
  - 修改文件：`src/infrastructure/sync/videoframescheduler.h`、`src/infrastructure/sync/videoframescheduler.cpp`、`tests/test_video_frame_scheduler.cpp`
  - 验收标准：快速 Stop/Start 后仍只有一个活动 Tick 链；视频 EOS 只发一次排空信号。
  - 测试流程：Qt 事件循环中执行 Start/Stop/Start 并统计回调。
  - Commit message：`fix: 统一视频调度器定时器生命周期`

- [ ] Phase 3.2：为摄像头补可中断 I/O。
  - 修改文件：`src/domain/CameraMediaSource.h`、`src/domain/CameraMediaSource.cpp`
  - 验收标准：Open 前安装 interrupt callback；Stop/Close 可无锁请求中断。
  - 测试流程：阻塞源单测与摄像头拔插人工验证。
  - Commit message：`fix: 增加摄像头阻塞读取中断路径`

## Phase 4：静态验收

- [ ] Phase 4.1：检查修改范围和调用链。
  - 验收标准：`git diff --check` 通过；无 UI、渲染、多路管理的无关修改；旧 EOS 立即 Stop 路径已移除。
  - 测试流程：执行定向 `rg` 和相关文件 `git status --short`。
  - Commit message：`test: 补充播放状态回归覆盖`

## 风险与沟通点

- 本轮按用户授权直接修复已确认缺陷，不修改公开 UI 交互。
- 默认只做静态检查；编译、自动测试和设备实测需在用户明确要求后执行。

