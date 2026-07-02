# TODO: Phase 5 图像处理基础

## 当前进度

- 已完成：
  - Phase 4 网络流支持已完成收口。
  - Phase 5.1 已完成图像处理基础预研和任务拆分。
  - Phase 5.2 已完成 OpenCV 依赖接入预检查。
  - Phase 5.3 已完成图像处理抽象空壳。
  - Phase 5.4 已完成帧与 OpenCV Mat 转换边界。
  - Phase 5.5 已完成 active stream 图像处理帧 sink 接入。
  - Phase 5.6 已完成基础图像处理器实现。
- 未完成：
  - 最小 UI 接入。
  - 性能和关闭链路验收。
- 已知风险：
  - OpenCV 当前使用项目内 `Lib/opencv`，只确认 VC2019 链路，不支持 MinGW kit。
  - `AVFrame -> cv::Mat -> AVFrame` 会增加 CPU 和内存带宽压力。
  - OpenCV 默认 BGR，当前渲染链路以 YUV420P 为主，需要明确转换边界。
  - 多路同时处理会显著放大性能压力，第一版不承诺。
- 本轮范围：
  - 新增灰度化、二值化、边缘检测处理器。
  - 保持处理器只访问 OpenCV Mat。
  - 不在处理器中访问 UI、队列、同步器或 OpenGL 资源。
  - 不接入 UI 控件。
  - 不改变播放链路和渲染链路。

## 阶段目标

在当前多路播放架构基础上增加基础实时图像处理能力。

第一版只处理 active stream，建立处理链路和基础处理器，不承诺多路同时处理。

推荐目标链路：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> active MediaPipeline
                -> VideoFrameScheduler
                    -> ImageProcessingFrameSink
                        -> ImageProcessorPipeline
                        -> SingleStreamFrameSinkAdapter
                            -> MultiViewRenderer
```

## 架构边界

- 不让 `MainWindow` 直接访问视频帧。
- 不让 `MainWindow` 直接持有图像处理器。
- 不让 `VideoFrameScheduler` 执行 OpenCV 算法。
- 不让 `MultiViewRenderer` 执行 OpenCV 算法。
- 不让图像处理模块访问 `VFrame_` 或 `SyncManager`。
- 不改变 `IMediaSource`、`demuxer`、`decoder` 的职责。
- 不把 `StreamId` 放进 `MediaPipeline`。
- 第一版只处理 active stream，不处理所有可见 stream。
- OpenCV 依赖必须先单独验证，再写算法逻辑。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase4_网络流支持验收报告.md`
- `doc/Phase3_13_渲染输入格式适配方案.md`
- `doc/Phase3_14_多路性能优化验收报告.md`
- `doc/Phase5_图像处理基础预研报告.md`
- `doc/Phase5_2_OpenCV依赖接入报告.md`

## Phase 5.1 图像处理基础预研和任务拆分

**目标**：明确图像处理模块的位置、职责边界、第一版范围和后续任务拆分。

**预计修改文件**：

- `doc/Phase5_图像处理基础预研报告.md`
- `doc/Phase5TODO.md`

**任务清单**：

- [x] 梳理当前播放与渲染链路。
- [x] 明确图像处理插入位置。
- [x] 明确为什么不放进 decoder。
- [x] 明确为什么不放进 `VideoFrameScheduler`。
- [x] 明确为什么不放进 `MultiViewRenderer`。
- [x] 明确第一版只处理 active stream。
- [x] 明确 OpenCV 依赖需要单独预检查。
- [x] 明确基础处理器范围。
- [x] 明确第一版不做范围。

**验收标准**：

- [x] 能说明图像处理链路应插在 scheduler 和 sink 之间。
- [x] 能说明 UI 不直接处理帧数据。
- [x] 能说明 MultiViewRenderer 仍只负责显示。
- [x] 能说明第一版为什么不做多路同时处理。

**测试流程**：

- [x] 本任务只写文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase5_图像处理基础预研报告.md doc\Phase5TODO.md`
- [x] 定向搜索：`rg -n "ImageProcessingFrameSink|ImageProcessorPipeline|IImageProcessor|OpenCV|active stream|MultiViewRenderer|VideoFrameScheduler" doc\Phase5_图像处理基础预研报告.md doc\Phase5TODO.md`

**Commit message**：

```text
docs: 增加 Phase 5 图像处理基础规划

- 新增 Phase 5 图像处理基础预研报告
- 新增 Phase 5 图像处理 TODO
- 明确图像处理链路插入 scheduler 与 sink 之间
- 明确第一版只处理 active stream
- 明确 OpenCV 依赖接入和基础处理器的后续任务
```

## Phase 5.2 OpenCV 依赖接入预检查

**目标**：确认当前工程如何安全引入 OpenCV，不和算法实现混在一起。

**预计修改文件**：

- `doc/Phase5_2_OpenCV依赖接入报告.md`
- `doc/Phase5TODO.md`

**任务清单**：

- [x] 确认当前主要 Qt kit 是 MSVC / VC2019。
- [x] 确认本机 OpenCV 安装位置为 `Lib/opencv`。
- [x] 确认 OpenCV 编译器 ABI 与 Qt kit 匹配：均为 VC2019。
- [x] 复核 `CMakeLists.txt` 接入方式。
- [x] 明确 Debug / Release 库路径风险。
- [x] 明确当前使用项目内固定路径，不改为全局 `find_package(OpenCV)`。
- [x] 形成依赖接入报告。

**验收标准**：

- [x] 能说明当前应该使用哪套 OpenCV。
- [x] 能说明 CMake 接入方式。
- [x] 能说明 Debug / Release 链接策略。
- [x] 不写图像处理算法。

**测试流程**：

- [x] 静态检查：`git diff --check -- doc\Phase5_2_OpenCV依赖接入报告.md doc\Phase5TODO.md`
- [x] 定向搜索：`rg -n "OpenCV|opencv_world4100|opencv_world4100d|VC2019|CMake|Lib/opencv|find_package|Phase 5.2" doc\Phase5_2_OpenCV依赖接入报告.md doc\Phase5TODO.md CMakeLists.txt`
- [ ] 用户人工编译验证 OpenCV 接入是否成功。

**Commit message**：

```text
docs: 预研 OpenCV 依赖接入方式

- 新增 OpenCV 依赖接入报告
- 梳理 Qt kit 与 OpenCV ABI 匹配风险
- 明确 CMake 接入策略和路径配置边界
- 为 Phase 5 图像处理算法实现准备依赖基础
```

## Phase 5.3 图像处理抽象空壳

**目标**：新增图像处理基础抽象，但不接入播放链路。

**预计修改文件**：

- `src/infrastructure/iimageprocessor.h`
- `src/infrastructure/imageprocessorpipeline.h`
- `src/infrastructure/imageprocessorpipeline.cpp`
- `doc/Phase5TODO.md`

**任务清单**：

- [x] 新增 `IImageProcessor`。
- [x] 定义 `Name()`。
- [x] 定义 `Enabled()`。
- [x] 定义 `SetEnabled()`。
- [x] 定义 `Process()`。
- [x] 新增 `ImageProcessorPipeline`。
- [x] 支持添加处理器。
- [x] 支持清空处理器。
- [x] 支持按顺序执行已启用处理器。
- [x] 支持记录最近一次处理耗时。
- [x] 不接入 `MediaPipeline`。
- [x] 不接入 UI。
- [x] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [x] 处理器抽象不依赖 UI。
- [x] 处理器管线不访问 `VFrame_`。
- [x] 处理器管线不访问 `SyncManager`。
- [x] 新增抽象职责单一。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\iimageprocessor.h src\infrastructure\imageprocessorpipeline.h src\infrastructure\imageprocessorpipeline.cpp doc\Phase5TODO.md`
- [x] 新文件空白检查：`git diff --check --no-index -- NUL src\infrastructure\iimageprocessor.h`
- [x] 新文件空白检查：`git diff --check --no-index -- NUL src\infrastructure\imageprocessorpipeline.h`
- [x] 新文件空白检查：`git diff --check --no-index -- NUL src\infrastructure\imageprocessorpipeline.cpp`
- [x] 定向搜索：`rg -n "IImageProcessor|ImageProcessorPipeline|Process|Enabled|SetEnabled|LastProcessElapsedMs|ProcessorCount" src doc\Phase5TODO.md`
- [ ] 用户人工编译验证。

**Commit message**：

```text
feat: 新增图像处理器基础抽象

- 新增 IImageProcessor 图像处理器接口
- 新增 ImageProcessorPipeline 管理处理器链
- 支持处理器启停和顺序执行
- 暂不接入播放链路和 UI
- 为 active stream 图像处理预览建立基础边界
```

## Phase 5.4 帧与 OpenCV Mat 转换边界

**目标**：建立 `AVFramePtr` 与 OpenCV `cv::Mat` 的转换边界，避免转换逻辑散落。

**预计修改文件**：

- `src/infrastructure/videoframematconverter.h`
- `src/infrastructure/videoframematconverter.cpp`
- `doc/Phase5TODO.md`

**任务清单**：

- [x] 新增 `VideoFrameMatConverter`。
- [x] 支持 `AVFramePtr -> cv::Mat`。
- [x] 支持 `cv::Mat -> AVFramePtr`。
- [x] 明确第一版中间格式为 `BGR24` / `CV_8UC3`。
- [x] 复制必要时间戳和色彩属性。
- [x] 转换失败返回空帧或 false。
- [x] 转换失败不停止播放。
- [x] 不让处理器保存帧内存指针。
- [x] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [x] 转换逻辑不在 UI。
- [x] 转换逻辑不在 `VideoFrameScheduler`。
- [x] 转换逻辑不在 `MultiViewRenderer`。
- [x] 所有权仍使用 `AVFramePtr`。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\videoframematconverter.h src\infrastructure\videoframematconverter.cpp doc\Phase5TODO.md`
- [x] 新文件空白检查：`git diff --check --no-index -- NUL src\infrastructure\videoframematconverter.h`
- [x] 新文件空白检查：`git diff --check --no-index -- NUL src\infrastructure\videoframematconverter.cpp`
- [x] 定向搜索：`rg -n "VideoFrameMatConverter|cv::Mat|AVFramePtr|av_frame_copy_props|AV_PIX_FMT_BGR24|CV_8UC3" src doc\Phase5TODO.md`
- [ ] 用户人工编译验证。

**Commit message**：

```text
feat: 增加视频帧与 OpenCV Mat 转换边界

- 新增 VideoFrameMatConverter
- 集中处理 AVFramePtr 与 cv::Mat 的转换
- 保持处理前后帧所有权由 RAII 管理
- 为图像处理链路避免转换逻辑散落
```

## Phase 5.5 ImageProcessingFrameSink 接入 active stream

**目标**：把图像处理链路插入 `VideoFrameScheduler` 和下游 sink 之间，默认关闭处理。

**预计修改文件**：

- `src/infrastructure/imageprocessingframesink.h`
- `src/infrastructure/imageprocessingframesink.cpp`
- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `doc/Phase5TODO.md`

**任务清单**：

- [x] 新增 `ImageProcessingFrameSink`。
- [x] `ImageProcessingFrameSink` 实现 `IVideoFrameSink`。
- [x] 支持设置下游 sink。
- [x] 支持启用/关闭处理。
- [x] 关闭处理时直接转发原帧。
- [x] 启用处理时调用 `ImageProcessorPipeline`。
- [x] `MediaPipeline` 持有处理 sink 或处理管线。
- [x] Controller 提供 active stream 处理开关接口。
- [x] 非 active stream 不启用处理。
- [x] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [x] 默认关闭处理时播放行为不变。
- [x] 开启处理只影响 active stream。
- [x] 关闭处理后恢复原图。
- [x] 切换 active stream 不崩溃。
- [x] remove / stop / shutdown 不阻塞。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\imageprocessingframesink.h src\infrastructure\imageprocessingframesink.cpp src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp src\domain\MultiStreamManager.h src\domain\MultiStreamManager.cpp src\application\MediaPlayController.h src\application\MediaPlayController.cpp doc\Phase5TODO.md`
- [x] 定向搜索：`rg -n "ImageProcessingFrameSink|SetImageProcessing|ImageProcessorPipeline|PresentFrame|SetActiveStream" src doc\Phase5TODO.md`
- [ ] 人工运行建议：普通 MP4 播放、暂停、停止、seek、关闭。
- [ ] 人工运行建议：多路播放，切换 active stream 后开启/关闭处理。

**Commit message**：

```text
feat: 接入 active stream 图像处理帧 sink

- 新增 ImageProcessingFrameSink 作为 IVideoFrameSink 装饰器
- 将图像处理链路插入调度器和渲染 sink 之间
- 默认关闭处理时直接转发原帧
- 通过 Controller 控制 active stream 的处理开关
- 保持 MultiViewRenderer 只负责显示
```

## Phase 5.6 基础图像处理器

**目标**：实现第一批基础处理器，用于验证处理链路。

**预计修改文件**：

- `src/infrastructure/grayscaleprocessor.h`
- `src/infrastructure/grayscaleprocessor.cpp`
- `src/infrastructure/thresholdprocessor.h`
- `src/infrastructure/thresholdprocessor.cpp`
- `src/infrastructure/edgedetectionprocessor.h`
- `src/infrastructure/edgedetectionprocessor.cpp`
- `doc/Phase5TODO.md`

**任务清单**：

- [x] 实现灰度化处理器。
- [x] 实现二值化处理器。
- [x] 实现边缘检测处理器。
- [x] 二值化支持阈值参数。
- [x] 边缘检测第一版使用固定参数。
- [x] 处理失败时返回 false。
- [x] 不在处理器中访问 UI。
- [x] 不在处理器中保存帧指针。
- [x] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [ ] 灰度化效果可见，待 Phase 5.7 UI 接入后人工验证。
- [ ] 二值化效果可见，待 Phase 5.7 UI 接入后人工验证。
- [ ] 边缘检测效果可见，待 Phase 5.7 UI 接入后人工验证。
- [ ] 关闭处理后画面恢复原图，待 Phase 5.7 UI 接入后人工验证。
- [x] 处理失败返回 false，不在处理器内部抛出异常或访问外部资源。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\grayscaleprocessor.h src\infrastructure\grayscaleprocessor.cpp src\infrastructure\thresholdprocessor.h src\infrastructure\thresholdprocessor.cpp src\infrastructure\edgedetectionprocessor.h src\infrastructure\edgedetectionprocessor.cpp doc\Phase5TODO.md`
- [x] 定向搜索：`rg -n "GrayscaleProcessor|ThresholdProcessor|EdgeDetectionProcessor|threshold|Canny|cvtColor" src doc\Phase5TODO.md`
- [ ] 人工运行建议：普通视频分别切换三种处理效果。

**Commit message**：

```text
feat: 增加基础图像处理器

- 新增灰度化处理器
- 新增二值化处理器
- 新增边缘检测处理器
- 支持通过图像处理管线执行基础 OpenCV 算法
- 保持处理器不访问 UI 和播放同步状态
```

## Phase 5.7 最小 UI 接入

**目标**：提供最小图像处理控制入口，验证处理链路可由用户操作。

**预计修改文件**：

- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `doc/Phase5TODO.md`

**任务清单**：

- [ ] 新增图像处理开关。
- [ ] 新增处理模式选择。
- [ ] 新增二值化阈值控件。
- [ ] UI 只调用 Controller。
- [ ] Controller 转发到 active stream。
- [ ] 没有 active stream 时控件安全无效。
- [ ] 实时源和文件源行为一致。

**验收标准**：

- [ ] 用户可以开启/关闭处理。
- [ ] 用户可以切换处理模式。
- [ ] 用户可以调整二值化阈值。
- [ ] UI 不直接访问处理器和帧。
- [ ] 多路下只影响 active stream。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui src\application\MediaPlayController.h src\application\MediaPlayController.cpp doc\Phase5TODO.md`
- [ ] 定向搜索：`rg -n "ImageProcessing|processing|Threshold|SetImageProcessing|MainWindow" src doc\Phase5TODO.md`
- [ ] 人工运行建议：文件源、摄像头源、RTSP 源分别开启处理。
- [ ] 人工运行建议：多路播放时切换 active stream，确认处理只影响当前 active stream。

**Commit message**：

```text
feat: 接入图像处理最小 UI 控制

- 在主窗口增加图像处理开关和模式选择
- 支持通过 Controller 控制 active stream 图像处理
- 支持调整二值化阈值
- 保持 UI 不直接访问视频帧和处理器实现
```

## Phase 5.8 性能与生命周期验收

**目标**：确认图像处理不会破坏播放、seek、多路、实时源和关闭释放链路。

**预计修改文件**：

- `doc/Phase5_图像处理基础验收报告.md`
- `doc/Phase5TODO.md`

**任务清单**：

- [ ] 验证文件播放开启处理。
- [ ] 验证无音频文件开启处理。
- [ ] 验证摄像头源开启处理。
- [ ] 验证 RTSP 源开启处理。
- [ ] 验证播放、暂停、停止、seek。
- [ ] 验证多路切换 active stream。
- [ ] 验证 remove stream。
- [ ] 验证关闭窗口。
- [ ] 记录 CPU / GPU / 内存变化。
- [ ] 记录处理耗时。
- [ ] 形成验收报告。

**验收标准**：

- [ ] 默认关闭处理时播放不回归。
- [ ] 开启处理后画面效果正确。
- [ ] seek 后不显示旧处理帧。
- [ ] remove stream 后不访问已释放帧。
- [ ] 关闭程序不阻塞。
- [ ] 性能开销有记录。

**测试流程**：

- [ ] 静态检查：`git diff --check -- doc\Phase5_图像处理基础验收报告.md doc\Phase5TODO.md`
- [ ] 人工运行建议：普通 MP4、无音频视频、摄像头、RTSP。
- [ ] 人工运行建议：多路 2x2 / 3x3，切换 active stream 后处理。

**Commit message**：

```text
docs: 完成 Phase 5 图像处理基础验收

- 增加 Phase 5 图像处理基础验收报告
- 记录基础处理器在文件、摄像头和 RTSP 源下的验证结果
- 记录多路 active stream 处理策略验证结果
- 记录处理耗时和资源占用变化
- 明确后续 Phase 6 高级图像分析入口
```

## 不做范围

- 不做多路同时图像处理。
- 不做每路独立处理器链。
- 不做处理器拖拽排序。
- 不做复杂参数面板。
- 不做 GPU 图像处理。
- 不做目标检测。
- 不做尺寸测量。
- 不做标定。
- 不做检测结果存储。
- 不做报表导出。

## 风险与沟通点

- OpenCV 依赖接入前，需要确认当前主力构建 kit。
- 如果用户希望支持 MSVC 和 MinGW 双 kit，需要额外设计依赖路径策略。
- 如果第一版处理效果要覆盖 9/16 路，需要重新评估性能边界。
- 如果后续要做工业视觉检测，应优先确认典型样本、光照条件、目标形态和验收指标。
