# TODO: Phase 4 网络流支持

## 当前进度

- 已完成：
  - Phase 3 多路播放主线已完成。
  - Phase 3.15 已完成 9/16 路真实播放扩容。
  - Phase 3.16 量化验收已作为项目完结阶段参考 TODO 暂缓。
  - Phase 4.1 已完成网络流输入预研和任务拆分。
  - Phase 4.2 已完成 `NetworkMediaSource` 空壳与参数模型，新增网络流实时源语义。
  - Phase 4.3 已完成 RTSP 单路 MVP 代码接入，等待人工播放验收。
- 未完成：
  - RTSP MVP 尚未完成运行验收。
  - RTMP / HTTP-FLV / MJPEG 尚未实现。
- 本阶段范围：
  - 优先接入 RTSP 网络流。
  - 复用现有 `IMediaSource -> MediaPipeline -> MultiViewRenderer` 链路。
  - 先保证单路 RTSP 可播放，再验证多路混合预览。

## 阶段目标

让系统支持通过 URL 添加 RTSP 网络流，并复用现有播放管线和多路渲染体系。

目标链路：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline
                -> NetworkMediaSource
                -> demuxer
                -> decoder
                -> VideoFrameScheduler
                -> MultiViewRenderer
```

## 架构边界

- 不新增网络流专用 `MediaPipeline`。
- 不让 `NetworkMediaSource` 管理解码、同步、渲染。
- 不让 `MultiViewRenderer` 感知 URL 或协议。
- 不让 `demuxer` 直接解析 URL 配置。
- 不在 Phase 4 MVP 中实现 ONVIF、工业 GigE 相机 SDK 或自动设备发现。
- 不把 RTMP / HTTP-FLV 写成已完成能力。
- 不为了网络流临时破坏文件源和摄像头源的现有路径。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase2_阶段性收尾报告.md`
- `doc/Phase3_15_9_16路真实播放扩容验收报告.md`
- `doc/Phase4_网络流输入预研报告.md`

## 4.1 网络流输入预研和任务拆分

**目标**：明确网络流协议边界、架构接入方式、RTSP MVP 范围和后续任务拆分。

**预计修改文件**：

- `doc/Phase4_网络流输入预研报告.md`
- `doc/Phase4TODO.md`

**任务清单**：

- [x] 明确 RTSP、RTMP、HTTP-FLV、MJPEG、HLS 的优先级。
- [x] 明确 RTSP 作为 MVP。
- [x] 明确工业 GigE 相机不归入普通 `NetworkMediaSource`。
- [x] 明确 `NetworkMediaSource` 实现 `IMediaSource`。
- [x] 明确网络流复用现有 `MediaPipeline`。
- [x] 明确网络流默认实时源语义。
- [x] 明确测试策略：公网 RTSP 冒烟 + 本地 MediaMTX / SRS 回归。

**验收标准**：

- [x] 能说明为什么先做 RTSP。
- [x] 能说明 RTMP / HTTP-FLV 为什么后置。
- [x] 能说明 `NetworkMediaSource` 和工业相机源的边界。
- [x] 能说明 Phase 4 MVP 的最小验收目标。

**测试流程**：

- [x] 本任务只写文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase4_网络流输入预研报告.md doc\Phase4TODO.md`
- [x] 定向搜索：`rg -n "RTSP|RTMP|HTTP-FLV|NetworkMediaSource|IMediaSource|MediaPipeline|MultiViewRenderer|实时源|MVP" doc\Phase4_网络流输入预研报告.md doc\Phase4TODO.md`

**Commit message**：

```text
docs: 增加 Phase 4 网络流输入预研

- 新增网络流输入预研报告
- 新增 Phase 4 网络流支持 TODO
- 明确 RTSP 作为第一阶段 MVP
- 规划 NetworkMediaSource 复用现有 MediaPipeline 和 MultiViewRenderer
```

## 4.2 NetworkMediaSource 空壳与参数模型

**目标**：新增网络流媒体源类型和打开参数模型，但先不接入 UI。

**预计修改文件**：

- `src/domain/NetworkMediaSource.h`
- `src/domain/NetworkMediaSource.cpp`
- `src/domain/NetworkTypes.h`
- `src/domain/IMediaSource.h`
- `CMakeLists.txt`
- `doc/Phase4TODO.md`

**任务清单**：

- [x] 新增 `NetworkStreamProtocol`。
- [x] 新增 `NetworkOpenOptions`。
- [x] 新增 `NetworkMediaSource` 类。
- [x] 实现 `Open()` / `Close()` / `GetFormatContect()`。
- [x] 实现 `LastError()`。
- [x] 实现 `SourceIdentity()`。
- [x] 实现 `IsSeekable() = false`。
- [x] 实现 `HasDuration() = false`。
- [x] 实现 `IsRealtime() = true`。
- [x] 将新文件加入构建。

**验收标准**：

- [x] `NetworkMediaSource` 职责只限打开和关闭网络媒体源。
- [x] URL 和协议参数不散落在 UI 或 demuxer。
- [x] 打开失败可以返回明确错误。
- [x] 不影响 `FileMediaSource` 和 `CameraMediaSource`。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\domain\NetworkMediaSource.h src\domain\NetworkMediaSource.cpp src\domain\NetworkTypes.h src\domain\IMediaSource.h CMakeLists.txt doc\Phase4TODO.md`
- [x] 定向搜索：`rg -n "NetworkMediaSource|NetworkOpenOptions|NetworkStreamProtocol|Network|IsRealtime|IsSeekable|HasDuration" src doc\Phase4TODO.md`
- [x] 本任务默认不编译，除非用户要求。
- [x] 构建接入说明：`CMakeLists.txt` 已通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新文件，本任务无需修改构建脚本。

**Commit message**：

```text
feat: 新增网络流媒体源基础模型

- 新增 NetworkMediaSource 和网络流打开参数结构
- 将网络流接入 IMediaSource 实时源语义
- 预留 RTSP/RTMP/HTTP-FLV 协议类型
- 保持 demuxer 和 MediaPipeline 继续依赖媒体源抽象
```

## 4.3 RTSP 单路最小可播放 MVP

**目标**：通过 URL 添加并播放单路 RTSP。

**预计修改文件**：

- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `doc/Phase4TODO.md`

**任务清单**：

- [x] 在 Controller 增加添加网络流入口。
- [x] 在 MainWindow 增加最小 URL 输入入口。
- [x] 使用 `NetworkOpenOptions` 构造 `NetworkMediaSource`。
- [x] 默认 RTSP over TCP。
- [x] 设置打开超时和读取超时。
- [x] 成功后复用现有 `AddStream()` 链路。
- [x] RTSP 显示为实时源，禁用 seek。
- [x] 无音频 RTSP 保持静音。

**验收标准**：

- [ ] 单路 RTSP 可以打开。
- [ ] 单路 RTSP 可以显示画面。
- [ ] Stop / Remove / Close 不阻塞。
- [ ] 文件播放不回归。
- [ ] 摄像头播放不回归。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\application\MediaPlayController.h src\application\MediaPlayController.cpp src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui doc\Phase4TODO.md`
- [x] 定向搜索：`rg -n "AddNetwork|NetworkMediaSource|rtsp|NetworkOpenOptions|IsRealtime|LIVE" src doc\Phase4TODO.md`
- [ ] 人工运行建议：使用公网 RTSP 冒烟流或本地 MediaMTX RTSP 流。

**Commit message**：

```text
feat: 支持单路 RTSP 网络流播放

- 增加网络流 URL 添加入口
- 使用 NetworkMediaSource 打开 RTSP over TCP
- 复用现有 MediaPipeline 完成解复用、解码和渲染
- 将 RTSP 按实时源处理并禁用 seek
```

## 4.4 RTSP 接入多路预览

**目标**：文件源、摄像头源、RTSP 源可以混合进入多路预览。

**预计修改文件**：

- `src/application/MediaPlayController.cpp`
- `src/domain/MultiStreamManager.cpp`
- `src/ui/mainwindow.cpp`
- `doc/Phase4TODO.md`

**任务清单**：

- [ ] 验证文件 + RTSP 混合添加。
- [ ] 验证摄像头 + RTSP 混合添加。
- [ ] 验证多路 RTSP 添加。
- [ ] 验证 active stream 切换。
- [ ] 验证 active audio stream 仍然最多一路输出。
- [ ] 验证 2x2 下 4 路混合预览。
- [ ] 验证 3x3 下 RTSP 与文件混合显示。

**验收标准**：

- [ ] 混合源不破坏 stream-slot 映射。
- [ ] RTSP 不破坏文件 seek 能力。
- [ ] 文件源不被实时源语义误伤。
- [ ] 多路关闭不阻塞。

**测试流程**：

- [ ] 静态检查：`git diff --check -- <本任务修改文件>`
- [ ] 人工运行建议：1 路文件 + 1 路摄像头 + 1 路 RTSP。
- [ ] 人工运行建议：4 路混合源 start / pause / stop / remove / close。

**Commit message**：

```text
feat: 支持 RTSP 与本地源混合多路预览

- 复用多路 stream 管理链路添加 RTSP 源
- 验证文件、摄像头和 RTSP 混合显示
- 保持 active stream 和 active audio stream 策略稳定
- 保持实时源和文件源的 seek 语义隔离
```

## 4.5 网络异常、超时和关闭链路

**目标**：处理网络流打开失败、运行中断流和关闭阻塞风险。

**预计修改文件**：

- `src/domain/NetworkMediaSource.h`
- `src/domain/NetworkMediaSource.cpp`
- `src/infrastructure/demuxer.cpp`
- `src/domain/MediaPipeline.cpp`
- `src/domain/MultiStreamManager.cpp`
- `src/application/MediaPlayController.cpp`
- `doc/Phase4TODO.md`

**任务清单**：

- [ ] 打开失败通过 `LastError()` 返回。
- [ ] 认证失败、URL 错误、连接超时有明确日志。
- [ ] 运行中断流通过 `DemuxErrorOccurred()` 上报。
- [ ] Stop / Remove / Shutdown 不等待无限网络读取。
- [ ] 网络流异常后可以移除 stream。
- [ ] 网络流异常后可以重新添加。

**验收标准**：

- [ ] 错误 URL 不导致程序崩溃。
- [ ] 断流不导致程序崩溃。
- [ ] 关闭程序不阻塞。
- [ ] 异常后资源可释放。

**测试流程**：

- [ ] 静态检查：`git diff --check -- <本任务修改文件>`
- [ ] 人工运行建议：错误 URL。
- [ ] 人工运行建议：播放中关闭本地 RTSP 服务。
- [ ] 人工运行建议：断流后 remove，再重新添加。

**Commit message**：

```text
fix: 完善 RTSP 网络流异常和关闭链路

- 增加网络流打开失败错误上报
- 复用 demuxer 运行期错误链路处理断流
- 避免 Stop、Remove 和 Shutdown 阻塞在网络读取
- 支持异常后移除并重新添加网络流
```

## 4.6 RTMP / HTTP-FLV / MJPEG 扩展评估

**目标**：在 RTSP 稳定后再决定是否扩展其他网络协议。

**预计修改文件**：

- `doc/Phase4_RTMP_HTTP扩展评估报告.md`
- `doc/Phase4TODO.md`

**任务清单**：

- [ ] 评估 RTMP 是否进入正式支持。
- [ ] 评估 HTTP-FLV 是否进入正式支持。
- [ ] 评估 MJPEG 是否适合作为摄像头兼容路径。
- [ ] 明确是否需要本地 MediaMTX / SRS 回归脚本。
- [ ] 明确是否需要协议选择 UI。

**验收标准**：

- [ ] 能决定 RTMP 是否继续做。
- [ ] 能决定 HTTP-FLV 是否继续做。
- [ ] 不把未实现协议写入 README 正式能力。

**测试流程**：

- [ ] 本任务可以先只写评估报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase4_RTMP_HTTP扩展评估报告.md doc\Phase4TODO.md`

**Commit message**：

```text
docs: 评估 RTMP 和 HTTP 网络流扩展

- 复核 RTSP MVP 后的协议扩展价值
- 评估 RTMP、HTTP-FLV 和 MJPEG 的支持边界
- 明确后续是否需要本地流媒体服务回归环境
- 避免 README 过度承诺未实现网络协议
```

## 4.7 Phase 4 验收报告

**目标**：收口网络流阶段，明确最终支持协议和能力边界。

**预计修改文件**：

- `doc/Phase4_网络流支持验收报告.md`
- `doc/Phase4TODO.md`

**任务清单**：

- [ ] 记录 RTSP 单路播放结果。
- [ ] 记录 RTSP 多路混合结果。
- [ ] 记录异常和关闭链路结果。
- [ ] 记录 RTMP / HTTP 是否进入正式支持。
- [ ] 记录后续真实网口摄像头验证建议。
- [ ] 记录简历可展示表述。

**验收标准**：

- [ ] 能说明当前正式支持哪些网络协议。
- [ ] 能说明 RTSP 与文件、摄像头混合预览是否稳定。
- [ ] 能说明网络异常处理边界。
- [ ] 能说明工业相机和 RTSP 摄像头的边界。

**测试流程**：

- [ ] 静态检查：`git diff --check -- doc\Phase4_网络流支持验收报告.md doc\Phase4TODO.md`
- [ ] 人工运行建议：公网 RTSP、本地 MediaMTX RTSP、文件 + RTSP 混合预览。

**Commit message**：

```text
docs: 增加 Phase 4 网络流支持验收报告

- 汇总 RTSP 单路和多路混合播放结果
- 记录网络异常、超时和关闭链路验证
- 明确 RTMP、HTTP-FLV 和工业相机的后续边界
- 更新网络流能力的简历展示结论
```

## 不做范围

- 不做 ONVIF 发现。
- 不做摄像头账号管理。
- 不做复杂网络参数持久化。
- 不做断线自动重连。
- 不做低延迟算法专项。
- 不做工业 GigE 相机 SDK。
- 不做网络流录制。
- 不做网络流转推。

## 风险与沟通点

- RTSP 关闭阻塞是最高优先级风险。
- 公网 RTSP 测试源不稳定，不能作为唯一验收依据。
- 本地 MediaMTX / SRS 环境更适合回归测试。
- 网络流无音频很常见，必须沿用现有无音频流策略。
- 多路 RTSP 性能不应在 MVP 阶段过度承诺，应留给 Phase 3.16 或后续量化验收。
