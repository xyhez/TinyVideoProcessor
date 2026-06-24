# TODO: Phase 3.9 MultiViewRenderer 多 slot 真实帧扩展

## 当前进度

- 已完成：
  - Phase 3.8 已完成 `MultiViewRenderer` slot 0 单路真实帧接入。
  - `SingleStreamFrameSinkAdapter` 已能把单路 `IVideoFrameSink` 调用转发为带 `StreamId` 的 `MultiViewRenderer` 调用。
  - `MultiViewRenderer` 已具备 YUV420P 纹理上传和 slot 0 绘制能力。
  - 播放列表 slot 0 路径已能进入统一渲染实验区域。
  - 默认 `VideoRender` 路径仍可作为回退路径。
- 未完成：
  - 第二路及后续路尚未全部进入 `MultiViewRenderer`。
  - `MultiViewRenderer` 尚未对多个 slot 绘制真实帧。
  - 外部 sink 路径下旧 `VideoRender` widget 的过渡策略尚未收口。
  - 多路 remove / stop / shutdown / layout 切换的真实帧生命周期仍需复核。
- 已知风险：
  - 每路 OpenGL 纹理必须在 `MultiViewRenderer` 的 OpenGL 上下文线程创建、更新和释放。
  - `AVFrame*` 从播放管线跨线程投递到统一渲染器时，所有权必须保持单一且可追踪。
  - renderer 销毁后不能再处理 queued `PresentFrame` 或 `ClearFrame`。
  - 多路接入后，slot 和 stream 的映射变化不能导致旧帧残留到错误 viewport。
  - 旧 `VideoRender` widget 如果仍保留在界面中，可能造成“同一路有两个显示出口”或布局占位混乱。
- 本轮范围：
  - 将 Phase 3.8 的单路真实帧路径扩展为多 slot 路径。
  - 先支持现有 Grid2x2 布局中的多路 YUV420P 绘制。
  - 继续保留 `VideoRender` 作为单路帧调度和默认回退路径。
  - 暂不抽出独立 `VideoFrameScheduler`。
  - 暂不处理非 YUV420P 格式转换。
  - 暂不做性能优化和零拷贝改造。

## 阶段目标

在不破坏当前单路播放、播放列表、多路文件、多路摄像头和默认 `VideoRender` 回退路径的前提下，让多个 stream 的真实视频帧进入同一个 `MultiViewRenderer`，并按 `StreamId -> slot -> viewport` 映射绘制到对应区域。

目标链路：

```text
MultiStreamManager 分配 StreamId
    -> 为每一路创建 SingleStreamFrameSinkAdapter(streamId)
        -> MediaPipeline / VideoRender 继续负责单路取帧和同步判断
            -> IVideoFrameSink::PresentFrame(frame)
                -> MultiViewRenderer::PresentFrame(streamId, frame)
                    -> stream render state
                    -> slot viewport
                    -> paintGL() 多 slot 绘制
```

## 架构边界

- `MediaPipeline` 继续保持单路职责。
- `MediaPipeline` 不持有 `StreamId`。
- `MediaPipeline` 不直接依赖 `MultiViewRenderer`。
- `MultiViewRenderer` 不访问 `VFrame_`。
- `MultiViewRenderer` 不访问 `SyncManager`。
- `MultiViewRenderer` 不负责音视频同步、取帧判断、seek 判断或队列消费。
- `VideoRender` 暂时继续承担单路帧调度、同步判断和默认显示回退。
- `SingleStreamFrameSinkAdapter` 负责把无 `StreamId` 的单路 sink 调用转换为带 `StreamId` 的统一渲染调用。
- `MultiStreamManager` / UI 层负责 stream 生命周期、slot 映射和 sink 注入。
- active audio 策略不在本阶段调整。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase3_8TODO.md`
- `doc/Phase3_8_MultiViewRenderer单路真实帧接入验收报告.md`
- `doc/Phase3_7_视频帧调度边界设计.md`
- `doc/Phase3_7_帧所有权与清理协议.md`
- `doc/Phase3_7_IVideoFrameSink接口草案.md`

## 3.9.1 多 slot 真实帧扩展 TODO 拆分

**目标**：建立 Phase 3.9 独立 TODO，明确多 slot 真实帧扩展的实施边界、任务顺序、验收标准和不做范围。

**预计修改文件**：

- `doc/Phase3_9TODO.md`
- `doc/TODO.md`

**任务清单**：

- [x] 明确 Phase 3.9 阶段目标。
- [x] 明确当前已完成和未完成内容。
- [x] 明确架构边界。
- [x] 拆分 3.9.2 到 3.9.7 任务。
- [x] 为每个任务补充修改文件、验收标准、测试流程和 commit message。
- [x] 在主 `doc/TODO.md` 中增加 Phase 3.9 入口。

**验收标准**：

- [x] 能说明 Phase 3.9 要把多路真实帧扩展到多个 slot。
- [x] 能说明为什么 `StreamId` 仍然不进入 `MediaPipeline`。
- [x] 能说明 `MultiViewRenderer` 只负责绘制，不负责同步和取帧。
- [x] 后续任务可以按 TODO 顺序逐步执行。

**测试流程**：

- [x] 本任务为文档拆分，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_9TODO.md doc\TODO.md`

**Commit message**：

```text
docs: 拆分 Phase 3.9 多 slot 真实帧扩展任务

- 新增 Phase 3.9 独立 TODO
- 明确多路真实帧进入 MultiViewRenderer 的阶段目标
- 明确 StreamId 仍不进入 MediaPipeline
- 明确 MultiViewRenderer 只负责绘制，不负责同步和取帧
- 拆分多 slot 设计、sink 接入、绘制扩展、旧 widget 过渡、生命周期复核和验收报告任务
- 在主 TODO 中增加 Phase 3.9 入口
```

## 3.9.2 多 slot 绘制方案确认

**目标**：在动代码前确认多 slot 绘制策略，避免直接从 slot 0 判断扩展成临时分支。

**预计修改文件**：

- `doc/Phase3_9_MultiViewRenderer多slot绘制方案.md`
- `doc/Phase3_9TODO.md`

**任务清单**：

- [x] 梳理 `StreamId -> slot` 的来源。
- [x] 梳理 `slot -> viewport` 的来源。
- [x] 明确 `paintGL()` 中遍历 slot 还是遍历 stream。
- [x] 明确无帧 stream、已清理 stream、无映射 stream 的绘制行为。
- [x] 明确纹理上传时机：`PresentFrame` 只接管帧，`paintGL` 或 OpenGL 上下文内函数负责上传。
- [x] 明确旧 slot 0 特判应如何移除。
- [x] 明确多 slot 绘制下的日志边界，只保留必要诊断输出。

**验收标准**：

- [x] 能说明多 slot 绘制不依赖 slot 0 特判。
- [x] 能说明 stream 与 slot 映射变化后如何避免残留旧帧。
- [x] 能说明空 slot、无帧 slot 和正常 slot 的绘制差异。
- [x] 能说明 OpenGL 资源创建、更新、释放仍在 renderer 上下文内完成。

**测试流程**：

- [x] 本任务为设计文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_9_MultiViewRenderer多slot绘制方案.md doc\Phase3_9TODO.md`

**Commit message**：

```text
docs: 设计 MultiViewRenderer 多 slot 绘制方案

- 新增多 slot 绘制方案文档
- 明确 StreamId 到 slot 再到 viewport 的映射链路
- 明确 paintGL 不再依赖 slot 0 特判
- 明确空 slot、无帧 stream 和无映射 stream 的绘制行为
- 明确 OpenGL 纹理上传和释放边界
```

## 3.9.3 取消 UI 层 slot 0 sink 限制

**目标**：让多路文件和摄像头流都可以按自己的 `StreamId` 创建 `SingleStreamFrameSinkAdapter`，不再只给第一路或 slot 0 注入统一渲染 sink。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_9TODO.md`

**任务清单**：

- [ ] 找出当前只给 `containerIndex == 0` 或 slot 0 创建 sink 的代码。
- [ ] 提取 UI 层创建 frame sink 的小型辅助函数。
- [ ] 多路文件新增时，为每一路创建对应 `StreamId` 的 adapter。
- [ ] 摄像头新增时，为每一路创建对应 `StreamId` 的 adapter。
- [ ] 播放列表切换时，保持当前 slot 的 adapter 语义一致。
- [ ] 不修改 `MediaPipeline` 的职责。
- [ ] 不改变 active audio 策略。

**验收标准**：

- [ ] 第二路及后续路也能获得指向 `MultiViewRenderer` 的 frame sink。
- [ ] `StreamId` 仍只存在于 manager / controller / UI / render adapter 边界。
- [ ] 默认回退路径仍能在未提供 sink 时走 `VideoRender`。
- [ ] UI 层没有散落重复的 slot 0 特判。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp doc\Phase3_9TODO.md`
- [ ] 人工运行建议：添加两个文件，确认两路都进入统一渲染区域。
- [ ] 人工运行建议：添加一个文件和一个摄像头，确认两路都进入统一渲染区域。
- [ ] 人工运行建议：切换播放列表文件，确认当前 slot 仍显示正确流。

**Commit message**：

```text
feat: 取消统一渲染接入的 slot 0 限制

- 调整 UI 层 frame sink 创建逻辑
- 多路文件流均可创建 SingleStreamFrameSinkAdapter
- 摄像头流也可创建对应 StreamId 的统一渲染 adapter
- 保持 StreamId 不进入 MediaPipeline
- 保持默认 VideoRender 回退路径不变
- 保持 active audio 策略不变
```

## 3.9.4 支持 MultiViewRenderer 多 slot 真实帧绘制

**目标**：让 `MultiViewRenderer` 根据当前 slot 映射绘制多个 stream 的真实帧，移除只绘制 slot 0 的限制。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `doc/Phase3_9TODO.md`

**任务清单**：

- [ ] 检查 `paintGL()` 中当前 slot 0 特判。
- [ ] 按 slot 遍历 viewport。
- [ ] 根据 slot 找到绑定的 `StreamId`。
- [ ] 根据 `StreamId` 找到 `StreamRenderState`。
- [ ] 有有效纹理时绘制真实帧。
- [ ] 无有效帧时绘制空 slot 背景。
- [ ] stream detach 或 slot remap 后清理旧纹理和旧帧。
- [ ] 保留不支持像素格式的保护逻辑。

**验收标准**：

- [ ] 多个 slot 可以同时显示不同 stream 的真实帧。
- [ ] 空 slot 仍显示空背景。
- [ ] remove stream 后对应 slot 不残留最后一帧。
- [ ] layout 切换后 viewport 正确，不拉伸到错误区域。
- [ ] OpenGL 纹理释放不跨错误线程。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_9TODO.md`
- [ ] 人工运行建议：两路文件同时播放。
- [ ] 人工运行建议：三路或四路文件同时播放。
- [ ] 人工运行建议：删除其中一路后继续播放。
- [ ] 人工运行建议：切换布局后观察 viewport。

**Commit message**：

```text
feat: 支持 MultiViewRenderer 多 slot 真实帧绘制

- 移除 slot 0 单路绘制限制
- 按 slot 和 StreamId 映射绘制多路真实帧
- 保留空 slot 背景绘制
- 在 stream detach 和 slot remap 时清理旧帧状态
- 保持 MultiViewRenderer 只负责绘制，不负责同步和取帧
```

## 3.9.5 收口旧 VideoRender widget 过渡策略

**目标**：明确外部 sink 路径下旧 `VideoRender` widget 是否显示、隐藏或只作为调度组件存在，避免真实帧同时出现在旧区域和统一渲染区域。

**预计修改文件**：

- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_9TODO.md`

**任务清单**：

- [ ] 梳理外部 sink 存在时 `VideoRender` 的职责。
- [ ] 确认 `VideoRender` widget 是否仍需要加入旧布局。
- [ ] 如果外部 sink 存在，避免把旧 render widget 作为可见播放区域。
- [ ] 保持默认路径未提供 sink 时行为不变。
- [ ] 保持 `VideoRender` 内部调度能力暂时可用。
- [ ] 记录该策略是过渡方案，后续 `VideoFrameScheduler` 抽取后再收敛。

**验收标准**：

- [ ] 同一路视频不会同时显示在旧 `QOpenGLWidget` 和 `MultiViewRenderer`。
- [ ] 默认无外部 sink 播放路径不受影响。
- [ ] 外部 sink 路径下 `VideoRender` 仍能提供调度，不承担最终可见显示。
- [ ] 该策略不要求提前抽出 `VideoFrameScheduler`。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp src\ui\mainwindow.cpp doc\Phase3_9TODO.md`
- [ ] 人工运行建议：默认单路播放仍能显示。
- [ ] 人工运行建议：多路统一渲染时旧播放区域不再显示重复画面。
- [ ] 人工运行建议：stop all / pause all / close 不阻塞。

**Commit message**：

```text
refactor: 收口统一渲染路径下的旧 VideoRender widget 策略

- 明确外部 frame sink 存在时 VideoRender 作为调度组件使用
- 避免同一路视频同时显示在旧 widget 和 MultiViewRenderer
- 保持默认 VideoRender 可见播放路径不变
- 为后续抽出 VideoFrameScheduler 保留过渡边界
```

## 3.9.6 多 slot 生命周期复核

**目标**：复核多路真实帧接入后的 remove、stop、pause、seek、layout 切换和 shutdown 路径，重点确认帧、纹理、adapter 和 renderer 的清理顺序。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `src/infrastructure/singlestreamframesinkadapter.h`
- `src/infrastructure/singlestreamframesinkadapter.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `doc/Phase3_9TODO.md`

**任务清单**：

- [ ] 检查 remove stream 是否调用 `DetachStream(streamId)`。
- [ ] 检查 stop stream 是否调用 `ClearFrame(streamId)` 或等价清理。
- [ ] 检查 stop all 是否覆盖所有 stream。
- [ ] 检查 renderer 销毁后 adapter 不再访问 renderer。
- [ ] 检查 queued `PresentFrame` 在 renderer 不存在时释放 frame。
- [ ] 检查 seek 后旧帧不残留。
- [ ] 检查 layout 切换后 slot 映射与帧状态一致。

**验收标准**：

- [ ] remove 任一路不会留下旧帧和旧纹理。
- [ ] stop all 后统一渲染区域清空。
- [ ] pause all 后关闭程序不阻塞。
- [ ] 播放完成后关闭程序不阻塞。
- [ ] seek 后不会长期显示 seek 前旧帧。
- [ ] renderer 销毁期间没有悬空 queued 调用访问已释放对象。

**测试流程**：

- [ ] 静态检查：`git diff --check -- <本任务修改文件>`
- [ ] 人工运行建议：两路播放后删除第二路。
- [ ] 人工运行建议：两路播放后 stop all，再关闭程序。
- [ ] 人工运行建议：两路播放后 pause all，再关闭程序。
- [ ] 人工运行建议：一路播放完成后关闭程序。
- [ ] 人工运行建议：seek 后观察多 slot 是否刷新到新位置。

**Commit message**：

```text
fix: 完善 MultiViewRenderer 多 slot 生命周期清理

- 复核多路 remove 和 stop 的 frame 清理路径
- 在 stream detach 时释放对应帧和纹理状态
- 防止 renderer 销毁后 adapter 访问悬空对象
- 确保 stop all 和 shutdown 能清空统一渲染状态
- 保持同步和取帧逻辑仍在单路播放链路内
```

## 3.9.7 多 slot 真实帧扩展验收报告

**目标**：收尾 Phase 3.9，记录多 slot 真实帧扩展的实际结果、限制和下一阶段是否进入 `VideoFrameScheduler` 抽取。

**预计修改文件**：

- `doc/Phase3_9_MultiViewRenderer多slot真实帧扩展验收报告.md`
- `doc/Phase3_9TODO.md`

**任务清单**：

- [ ] 记录两路、三路、四路真实帧显示结果。
- [ ] 记录文件 + 摄像头混合显示结果。
- [ ] 记录 remove / stop / pause / seek / shutdown 验证结果。
- [ ] 记录旧 `VideoRender` widget 过渡策略是否成立。
- [ ] 记录非 YUV420P、性能和 `VideoFrameScheduler` 未处理限制。
- [ ] 判断是否进入下一阶段：抽离 `VideoFrameScheduler` 或继续优化 `MultiViewRenderer`。

**验收标准**：

- [ ] 能说明多 slot 真实帧接入是否成功。
- [ ] 能说明默认回退路径是否仍可用。
- [ ] 能说明当前剩余限制。
- [ ] 能说明下一阶段推荐方向。

**测试流程**：

- [ ] 本任务为验收报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_9_MultiViewRenderer多slot真实帧扩展验收报告.md doc\Phase3_9TODO.md`

**Commit message**：

```text
docs: 增加 MultiViewRenderer 多 slot 真实帧扩展验收报告

- 新增 Phase 3.9 多 slot 真实帧扩展验收报告
- 记录多路文件和文件摄像头混合显示结果
- 记录 remove、stop、pause、seek 和 shutdown 验证情况
- 记录旧 VideoRender widget 过渡策略结果
- 明确非 YUV420P、性能和 VideoFrameScheduler 抽取限制
- 判断下一阶段推进方向
```

## 不做范围

- 不抽出独立 `VideoFrameScheduler`。
- 不把 `StreamId` 放进 `MediaPipeline`。
- 不让 `MultiViewRenderer` 访问 `VFrame_`。
- 不让 `MultiViewRenderer` 访问 `SyncManager`。
- 不让 `MultiViewRenderer` 直接消费 packet queue 或 frame queue。
- 不调整 active audio 策略。
- 不处理非 YUV420P 格式转换。
- 不做硬件解码帧的零拷贝渲染。
- 不做性能优化和纹理复用策略优化。

## 风险与沟通点

- 如果旧 `VideoRender` widget 的过渡策略需要改变 `MediaPipeline` 对 render widget 的公开接口，应先确认影响范围。
- 如果多 slot 绘制暴露出像素格式不一致问题，本阶段只记录限制，不扩大到格式归一化。
- 如果 queued frame 在 renderer 销毁后仍进入 adapter，应优先修正生命周期，不通过简单忽略掩盖所有权问题。
- 如果多路统一渲染后发现音频切换策略影响用户感知，应单独进入 active audio 任务，不并入本阶段。
