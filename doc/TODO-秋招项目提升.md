# TODO-秋招项目提升

## 当前进度

- 已完成：播放器基础能力、文件/摄像头/RTSP 三类媒体源、多路播放管理、统一 OpenGL 多视口渲染、9 路正式播放、16 路实验播放、OpenCV 图像处理、YOLO ONNX 视觉分析、视觉参数配置化。
- 当前代码规模：约 88 个源码/UI 文件，约 1.72 万物理行，排除空行和纯注释后约 1.12 万行实现代码；不计入 `Lib/` 下的 FFmpeg、OpenCV 第三方代码。
- 已完成：建立 Qt Test + CTest 基础设施，新增 4 个测试目标和 18 个测试用例，覆盖 `SyncQueue`、`PlaylistManager`、`VisionConfigLoader`、`VisionPipelineManager`；MSVC Debug 下 4/4 测试目标通过。
- 当前主要短板：尚未建立 CI，FFmpeg 解码、Seek、网络异常、渲染和完整播放链路仍以人工验收为主；9/16 路缺少可复现量化性能表；YOLO 同步推理会阻塞播放调度；构建与运行路径依赖本机源码目录；视觉配置部分字段未真正生效；缺少可直接展示的截图和演示视频。
- 当前秋招定位：满足 C++/Qt、多媒体和边缘视觉方向秋招项目要求，整体处于中上水平；完成本 TODO 的 P0/P1 项目后，目标是从“功能完整的个人项目”提升为“证据充分、可复现、可交付的工程项目”。
- 本轮范围：建立 Qt Test + CTest 单元测试基础设施，覆盖线程队列、播放列表、视觉配置加载和视觉管线管理器；暂不建立 CI。

## 优先级说明

- `P0`：秋招投递前必须完成，直接决定项目可信度。
- `P1`：建议完成，直接提升技术深度和工程评价。
- `P2`：时间允许时完成，用于提高仓库整洁度和长期可维护性。
- `P3`：后续业务扩展，不作为当前秋招版本阻塞项。

## Phase 1：自动化测试与持续集成（P0）

### Phase 1.1 建立独立测试目标

- [x] 在 CMake 中增加测试开关和独立测试可执行目标。
  - 目标：让纯逻辑模块能够在不启动完整播放器 UI 的情况下运行测试。
  - 预计修改文件：
    - `CMakeLists.txt`
    - `tests/CMakeLists.txt`
    - `tests/README.md`
    - `tests/test_sync_queue.cpp`
    - `tests/test_playlist_manager.cpp`
    - `tests/test_vision_config_loader.cpp`
    - `tests/test_vision_pipeline_manager.cpp`
  - 验收标准：
    - CMake 支持通过选项启用或关闭测试。
    - `ctest --test-dir <build-dir> --output-on-failure` 能发现至少一个测试。
    - 默认应用构建不依赖测试框架运行时。
  - 测试流程：
    1. 分别配置启用测试和关闭测试的构建目录。
    2. 编译测试目标。
    3. 运行 CTest 并保存输出。
  - 实施结果：
    - 新增 `TINY_VIDEO_BUILD_TESTS`，默认关闭，不影响只构建应用的使用者。
    - 启用后加载 Qt Test、CTest 和 4 个独立测试目标。
    - 使用 `build/unit-tests-msvc-debug` 完成独立配置和编译。
  - Commit message：

    ```text
    test: 建立项目自动化测试基础设施

    - 增加独立测试目标和 CTest 接入
    - 支持通过 CMake 选项启用测试
    - 添加最小测试入口
    ```

### Phase 1.2 覆盖纯逻辑模块

- [x] 为线程队列、播放列表、视觉配置和视觉管线管理器补充单元测试。
  - 目标：优先覆盖无需真实视频、摄像头、OpenGL 上下文和 YOLO 模型的确定性逻辑。
  - 预计修改文件：
    - `tests/core/test_sync_queue.cpp`
    - `tests/domain/test_playlist_manager.cpp`
    - `tests/vision/test_vision_config_loader.cpp`
    - `tests/vision/test_vision_pipeline_manager.cpp`
    - 必要时小范围调整对应生产代码的可测试接口
  - 验收标准：
    - `SyncQueue` 覆盖正常入队出队、容量阻塞、等待中断、停止和恢复。
    - `PlaylistManager` 覆盖增删、上下项、索引修正和空列表行为。
    - `VisionConfigLoader` 覆盖合法配置、缺失文件、非法 JSON、重复 ID、未知类型和参数默认值合并。
    - `VisionPipelineManager` 覆盖注册、重复注册、激活切换、启动失败和关闭行为。
    - 测试不依赖用户机器上的摄像头、网络流或模型文件。
  - 测试流程：
    1. 连续运行完整测试集至少 10 次。
    2. 对队列测试设置超时，确保失败时不会无限等待。
    3. 保存测试用例数量、通过率和总耗时。
  - 实施结果：
    - `SyncQueue`：4 个用例，覆盖顺序、满队列阻塞、等待中断、停止与恢复。
    - `PlaylistManager`：5 个用例，覆盖初始状态、去重、删除索引修正、播放模式和信号。
    - `VisionConfigLoader`：5 个用例，覆盖缺失、非法 JSON、默认参数合并、重复 ID 和相对路径。
    - `VisionPipelineManager`：4 个用例，覆盖注册顺序、重复注册、生命周期切换、启动失败和未知 ID。
    - CTest 首轮结果：4/4 测试目标通过，耗时约 0.79 秒。
    - 使用 `--repeat until-fail:10` 连续执行 10 轮，40 次目标执行全部通过。
  - Commit message：

    ```text
    test: 覆盖核心队列和视觉配置逻辑

    - 增加 SyncQueue 与 PlaylistManager 单元测试
    - 覆盖视觉配置加载和管线切换
    - 验证错误配置和生命周期边界
    ```

### Phase 1.3 建立 Windows CI

- [ ] 在 CI 中自动执行配置、编译和单元测试。
  - 目标：证明仓库在独立环境中可以重复构建，而不是只在本机 QtCreator 配置下可用。
  - 预计修改文件：
    - `.github/workflows/windows-build.yml`
    - 必要的依赖缓存或安装脚本
  - 验收标准：
    - 推送和 Pull Request 都能触发 CI。
    - CI 至少执行 Debug 或 Release 中的一种构建。
    - 编译失败或测试失败时 CI 正确标红。
    - README 可展示最新 CI 状态。
  - 测试流程：
    1. 提交一版正常代码，确认 CI 通过。
    2. 在临时分支制造测试失败，确认 CI 能拦截。
    3. 恢复正常代码并确认重新通过。
  - Commit message：

    ```text
    ci: 增加 Windows 自动构建与测试

    - 配置 Qt CMake 构建工作流
    - 自动运行核心单元测试
    - 缓存第三方依赖以缩短构建时间
    ```

## Phase 2：可复现性能基线与稳定性验证（P0）

### Phase 2.1 固定测试环境和素材规格

- [ ] 建立统一性能测试说明和素材清单。
  - 目标：让 4/9/16 路性能结论具备输入规格和机器配置上下文。
  - 预计修改文件：
    - `doc/性能测试方案.md`
    - `doc/性能测试素材清单.md`
  - 验收标准：
    - 记录 CPU、GPU、内存、操作系统、Qt、FFmpeg、OpenCV 和编译模式。
    - 每个测试素材记录编码、分辨率、帧率、码率、像素格式、音频情况和时长。
    - 明确 4 路、9 路和 16 路的基础场景与压力场景。
    - 不把不同规格素材的结果直接横向比较。
  - 测试流程：
    1. 使用 `ffprobe` 生成素材信息。
    2. 人工复核素材实际可播放。
    3. 将测试环境和素材版本写入报告。
  - Commit message：

    ```text
    docs: 建立多路播放性能测试基线

    - 固定测试机器和依赖版本
    - 记录测试素材编码与规格
    - 定义 4/9/16 路测试场景
    ```

### Phase 2.2 增加运行时性能统计

- [ ] 补充可导出的播放与渲染统计数据。
  - 目标：减少仅依赖任务管理器截图的主观判断。
  - 预计修改文件：
    - `src/infrastructure/sync/videoframescheduler.h`
    - `src/infrastructure/sync/videoframescheduler.cpp`
    - `src/infrastructure/rendering/multiviewrenderer.h`
    - `src/infrastructure/rendering/multiviewrenderer.cpp`
    - 可选新增 `src/infrastructure/metrics/` 模块
  - 验收标准：
    - 每路至少统计解码/调度输入帧、渲染帧、丢帧、平均显示 FPS。
    - 渲染器至少统计纹理上传次数、纹理重建次数和绘制次数。
    - 统计输出带时间窗口和 `StreamId`，能够导出到日志或 CSV。
    - 统计逻辑本身不会造成明显日志刷屏或性能退化。
  - 测试流程：
    1. 分别运行 1/4/9/16 路场景至少 5 分钟。
    2. 对照人工观察确认统计数据数量级合理。
    3. 关闭统计后复测，确认统计开销可接受。
  - Commit message：

    ```text
    feat: 增加多路播放性能统计

    - 统计每路渲染和丢帧数据
    - 输出纹理上传与重建指标
    - 支持按时间窗口记录性能结果
    ```

### Phase 2.3 形成 4/9/16 路量化报告

- [ ] 完成可复现性能与生命周期验收报告。
  - 目标：为简历中的多路能力提供可核验数字和明确边界。
  - 预计修改文件：
    - `doc/多路播放性能验收报告.md`
    - `README.md`
  - 验收标准：
    - 每个场景记录平均 FPS、丢帧率、CPU、GPU、峰值内存、启动时间和关闭耗时。
    - 9 路正式能力写明通过规格，不泛化为任意 9 路视频。
    - 16 路明确保持实验能力，并记录降级条件。
    - README 中“当前实测”和“后续目标”严格分开。
  - 测试流程：
    1. 每个场景至少重复 3 次。
    2. 每次连续运行不少于 10 分钟。
    3. 执行播放、暂停、停止、移除、重新添加和关闭窗口。
    4. 保存原始日志和汇总表。
  - Commit message：

    ```text
    docs: 完成多路播放量化性能验收

    - 记录 4/9/16 路实测指标
    - 明确正式能力和实验能力边界
    - 更新 README 性能数据口径
    ```

## Phase 3：视觉推理异步化（P1）

### Phase 3.1 设计异步推理状态模型

- [ ] 明确显示帧率、推理帧率和结果时效性的关系。
  - 目标：避免在没有线程和所有权协议的情况下直接把 YOLO 移入后台线程。
  - 预计修改文件：
    - `doc/Phase9_视觉推理异步化设计.md`
    - `doc/TODO-视觉推理异步化.md`
  - 验收标准：
    - 明确输入帧所有权和 `cv::Mat` 深浅拷贝边界。
    - 明确单生产者/单消费者或任务覆盖策略。
    - 明确只保留最新待推理帧，避免慢模型导致任务无限堆积。
    - 明确结果缓存结构、PTS 匹配、过期策略和关闭顺序。
    - 明确模型加载和释放发生在哪个线程。
  - 测试流程：
    1. 用时序图复核启动、提交、覆盖、取结果、切换 pipeline 和关闭流程。
    2. 列出至少 5 个并发风险场景并给出处理策略。
    3. 设计通过后再进入实现。
  - Commit message：

    ```text
    docs: 设计视觉推理异步执行模型

    - 明确帧所有权和任务覆盖策略
    - 设计结果缓存与过期机制
    - 定义启动切换和关闭时序
    ```

### Phase 3.2 实现异步推理与最新结果缓存

- [ ] 将视觉推理从显示调度链路中移出。
  - 目标：复杂 YOLO 模型推理时保持播放、交互和渲染线程响应。
  - 预计修改文件：
    - `src/infrastructure/vision/visionprocessingframesink.h`
    - `src/infrastructure/vision/visionprocessingframesink.cpp`
    - `src/infrastructure/vision/visionpipelinemanager.h`
    - `src/infrastructure/vision/visionpipelinemanager.cpp`
    - 新增异步 worker 与结果缓存文件
  - 验收标准：
    - 显示链路提交帧后不等待 `net.forward()` 完成。
    - 同一时刻只保留有限数量待处理任务。
    - 没有新结果时可复用未过期的上一份结果。
    - 切换 pipeline、移除 stream、关闭程序时无死锁和悬空访问。
    - 推理失败不影响原始画面继续显示。
  - 测试流程：
    1. 使用正常模型和人为增加延迟的假 pipeline 测试。
    2. 连续切换 OpenCV/YOLO/关闭视觉分析至少 100 次。
    3. 在推理进行中移除 active stream 并关闭程序。
    4. 对比改造前后的 UI 响应、显示 FPS 和推理 FPS。
  - Commit message：

    ```text
    feat: 支持视觉管线异步推理

    - 将模型推理移出显示调度链路
    - 增加最新结果缓存和任务覆盖
    - 完善切换与关闭生命周期
    ```

### Phase 3.3 增加推理降帧和耗时指标

- [ ] 支持独立配置推理 FPS 和结果过期时间。
  - 目标：让播放帧率与推理帧率解耦，并形成可说明的性能数据。
  - 预计修改文件：
    - `config/vision_pipelines.json`
    - `src/infrastructure/vision/visionpipelineconfig.h`
    - `src/infrastructure/vision/visionpipelineconfig.cpp`
    - 对应 worker、controller 和 UI 文件
  - 验收标准：
    - 可配置目标推理 FPS。
    - UI 或日志显示最近耗时、平均耗时、推理 FPS 和跳过帧数。
    - 参数非法时回退安全默认值。
    - 关闭 overlay 时是否继续推理具有明确配置和 UI 语义。
  - 测试流程：
    1. 分别设置 1/5/10/不限 FPS。
    2. 验证显示 FPS 不随目标推理 FPS 同步下降。
    3. 对非法、缺失和极端配置执行回退测试。
  - Commit message：

    ```text
    feat: 支持视觉推理降帧与耗时统计

    - 配置独立推理帧率
    - 统计推理耗时和跳过帧数
    - 增加结果过期控制
    ```

## Phase 4：构建、发布与仓库治理（P1）

### Phase 4.1 修复运行时资源路径

- [ ] 移除对编译机器源码绝对路径的运行时依赖。
  - 目标：发布后的程序从可执行文件目录或用户配置目录查找配置、模型和日志。
  - 预计修改文件：
    - `CMakeLists.txt`
    - `src/infrastructure/vision/visionconfigloader.cpp`
    - `src/infrastructure/vision/visionprocessorfactory.cpp`
    - `src/infrastructure/vision/yolovisionpipeline.cpp`
    - TinyLog 路径初始化相关文件
  - 验收标准：
    - 可执行文件移动到独立目录后仍能读取配置。
    - 开发环境和发布环境使用同一套路径解析规则。
    - 日志写入用户可写目录，不要求源码目录存在。
    - 不在可执行文件中嵌入开发者本机项目路径。
  - 测试流程：
    1. 在源码目录外创建干净发布目录。
    2. 复制可执行文件、DLL、配置和模型。
    3. 分别验证有配置、无配置、非法配置和无模型场景。
  - Commit message：

    ```text
    build: 修复发布环境资源路径解析

    - 移除源码绝对路径运行时依赖
    - 统一配置模型和日志目录
    - 支持独立发布目录运行
    ```

### Phase 4.2 整理 CMake 与本地 IDE 文件

- [ ] 清理历史项目名、IDE 用户配置和非必要源文件收集。
  - 目标：让仓库构建描述与 TinyVideoProcessor 当前定位一致。
  - 预计修改文件：
    - `CMakeLists.txt`
    - `.gitignore`
    - Git 索引中的 `CMakeLists.txt.user`
  - 验收标准：
    - CMake project/target 命名统一。
    - `doc/*.md` 不再作为编译源文件参与 glob。
    - `CMakeLists.txt.user` 从 Git 跟踪中移除，但不删除用户本地配置。
    - 增加合理编译警告选项，项目在现有编译器下无新增警告。
  - 测试流程：
    1. 删除全新构建目录后重新配置。
    2. 分别执行 Debug 和 Release 构建。
    3. 检查 Git 工作区不会因 QtCreator 自动更新而持续变脏。
  - Commit message：

    ```text
    build: 整理 CMake 和 IDE 本地配置

    - 统一 TinyVideoProcessor 构建目标命名
    - 移除文档源文件 glob
    - 停止跟踪 QtCreator 用户配置
    ```

### Phase 4.3 生成可交付 Windows 发布包

- [ ] 建立 Release 打包和最小运行说明。
  - 目标：让面试官或其他开发者无需本地 QtCreator 配置即可运行项目。
  - 预计修改文件：
    - `CMakeLists.txt`
    - `cmake/` 下新增部署脚本
    - `README.md`
    - 可选 `LICENSE`、`THIRD_PARTY_NOTICES.md`
  - 验收标准：
    - 发布包包含应用、Qt/FFmpeg/OpenCV 必需 DLL 和默认配置。
    - 模型缺失时 OpenCV 管线仍可运行，YOLO 给出明确错误。
    - 在未安装 Qt 和开发工具的 Windows 环境中可以启动。
    - 第三方依赖版本和许可证说明完整。
  - 测试流程：
    1. 在干净 Windows 用户或虚拟机中解压运行。
    2. 验证本地文件、摄像头、RTSP、OpenCV 管线。
    3. 验证缺失 DLL、配置和模型时的错误提示。
  - Commit message：

    ```text
    build: 增加 Windows Release 发布包

    - 自动收集运行时依赖和默认配置
    - 补充第三方依赖说明
    - 验证干净环境启动流程
    ```

## Phase 5：架构与代码质量收口（P2）

### Phase 5.1 修复视觉配置字段语义

- [ ] 处理当前已读取但未生效的配置字段。
  - 目标：保证配置文件公开字段与程序实际行为一致。
  - 预计修改文件：
    - `config/vision_pipelines.json`
    - `src/infrastructure/vision/visionpipelineconfig.*`
    - `src/infrastructure/vision/visionprocessorfactory.*`
    - `src/infrastructure/vision/opencvcontourvisionpipeline.*`
    - `src/infrastructure/vision/yolovisionpipeline.*`
  - 验收标准：
    - `id` 和 `displayName` 要么真正注入 pipeline，要么从公开配置中删除并记录固定语义。
    - `backend` 和 `target` 要么真正控制 OpenCV DNN，要么从第一版配置中删除。
    - 重复固定类型 pipeline 的支持边界明确。
    - README 与实际配置能力一致。
  - 测试流程：
    1. 修改 ID、显示名、backend 和 target 并检查行为。
    2. 覆盖重复 ID、重复类型、未知值和禁用全部管线场景。
    3. 运行视觉配置单元测试。
  - Commit message：

    ```text
    fix: 统一视觉配置字段与运行时语义

    - 处理管线 ID 和显示名配置
    - 明确 DNN backend 与 target 支持边界
    - 更新配置校验和文档
    ```

### Phase 5.2 收紧队列并发接口

- [ ] 审查并修复 `SyncQueue` 的泛型和线程安全边界。
  - 目标：消除未加锁容量修改、锁外裸指针观察和对 `nullptr` 的隐式模板约束。
  - 预计修改文件：
    - `src/core/queue.h`
    - `tests/core/test_sync_queue.cpp`
  - 验收标准：
    - `SetMaxSize` 在锁内修改并正确唤醒等待者。
    - 不再提供锁释放后可能悬空的 `FrontRaw`，或用安全访问回调替代。
    - 队列模板对支持类型的约束明确。
    - 容量 0、停止、恢复、清空和中断行为有测试定义。
  - 测试流程：
    1. 执行多生产者/多消费者压力测试。
    2. 在等待过程中修改容量、停止和清空。
    3. 使用 ThreadSanitizer 可用环境执行数据竞争检查。
  - Commit message：

    ```text
    refactor: 收紧同步队列线程安全边界

    - 修复容量修改同步问题
    - 移除不安全裸指针观察接口
    - 增加队列并发压力测试
    ```

### Phase 5.3 缩小 UI 和领域层耦合

- [ ] 重新确认 `MediaPipeline`、`MainWindow` 和渲染设施的职责边界。
  - 目标：让文档中的分层描述与实际依赖方向更一致。
  - 预计修改文件：
    - `doc/架构指导.md`
    - `src/domain/MediaPipeline.*`
    - `src/application/MediaPlayController.*`
    - `src/ui/mainwindow.*`
    - 渲染 sink 创建与 slot 映射相关文件
  - 验收标准：
    - `MediaPipeline` 不直接创建或挂载 `QWidget`/`VideoRender`，渲染 sink 由上层注入。
    - UI 不直接控制解码、同步和线程生命周期。
    - slot 映射保留在 UI/application 的明确边界内。
    - 不以一次性大重构完成，按可运行步骤迁移。
  - 测试流程：
    1. 每次迁移后运行单路默认渲染、多路渲染和无音频视频。
    2. 验证文件、摄像头、RTSP 都能使用统一 sink 注入路径。
    3. 运行关闭窗口、移除流和切换 active stream 回归测试。
  - Commit message：

    ```text
    refactor: 解耦播放管线与具体 UI 渲染器

    - 统一通过帧 sink 注入渲染目标
    - 移除领域层 QWidget 依赖
    - 收紧 UI 与播放基础设施边界
    ```

### Phase 5.4 统一命名、编码和大文件职责

- [ ] 清理历史命名、乱码注释和过大的 UI/controller 文件。
  - 目标：降低面试现场阅读代码时的理解成本。
  - 预计修改文件：
    - `src/infrastructure/decoding/decoder.*`
    - `src/infrastructure/decoding/demuxer.*`
    - `src/domain/MediaPipeline.*`
    - `src/application/MediaPlayController.*`
    - `src/ui/mainwindow.*`
  - 验收标准：
    - 修复乱码注释并统一 UTF-8。
    - `ReAcoderPar`、`AdecoderTH_` 等历史命名按统一规范迁移。
    - 拆分只在职责明确且能降低维护成本时进行。
    - 每个重命名提交保持可编译，避免混入行为修改。
  - 测试流程：
    1. 每批重命名后执行完整编译和测试。
    2. 使用 `rg` 确认旧符号无残留。
    3. 对播放、Seek、多路和关闭链路执行人工回归。
  - Commit message：

    ```text
    refactor: 统一媒体管线命名与源码编码

    - 修复解码模块乱码注释
    - 统一线程和解码器成员命名
    - 拆分过长 UI 辅助职责
    ```

## Phase 6：秋招展示与面试证据包（P1）

### Phase 6.1 完善 README 展示材料

- [ ] 增加真实截图、短视频/GIF 和可复现运行步骤。
  - 目标：让仓库访问者在 1 分钟内理解项目效果和核心难点。
  - 预计修改文件：
    - `README.md`
    - `docs/assets/` 或 `doc/assets/`
  - 验收标准：
    - 至少展示单路播放、9 路布局、摄像头/RTSP、OpenCV 和 YOLO。
    - 图片和视频来自当前版本，不使用仅设计稿。
    - README 给出依赖、构建、模型准备、运行和常见错误说明。
    - 展示材料不包含隐私、版权风险素材或本机路径。
  - 测试流程：
    1. 在 GitHub 页面检查所有图片和链接。
    2. 按 README 在新构建目录执行完整步骤。
    3. 邀请一名不了解项目的人按文档启动程序并记录阻塞点。
  - Commit message：

    ```text
    docs: 完善项目运行与效果展示

    - 增加多路播放和视觉分析截图
    - 补充构建模型和运行步骤
    - 记录常见问题与能力边界
    ```

### Phase 6.2 统一简历数据口径

- [ ] 形成一页项目数据卡和三条可核验证据。
  - 目标：保证简历、README、面试自述和代码仓库使用同一组数字。
  - 预计修改文件：
    - `doc/面试复盘.md`
    - `README.md`
    - `doc/秋招项目数据卡.md`
  - 验收标准：
    - 开发时间区分早期原型和集中开发阶段，与 Git 历史一致。
    - 代码量明确排除第三方库和生成文件。
    - 9/16 路能力携带测试规格和正式/实验标签。
    - CPU、GPU、内存、延迟只引用实测数据，不把目标值写成结果。
    - “配置化”描述与实际支持字段一致。
  - 测试流程：
    1. 逐条从简历描述跳转到代码、测试或报告证据。
    2. 删除无法解释或无法复现的数字。
    3. 完成一次 5 分钟项目介绍和 20 分钟深挖模拟。
  - Commit message：

    ```text
    docs: 统一秋招项目数据和面试口径

    - 补充代码量与开发周期数据
    - 关联多路性能实测证据
    - 明确正式能力和已知限制
    ```

## Phase 7：后续业务扩展（P3）

- [ ] RTSP 自动断线重连。
- [ ] RTMP 和 HTTP-FLV 输入。
- [ ] 摄像头截图、录像和配置持久化。
- [ ] 视觉结果事件、报警、存储和历史查询。
- [ ] OCR 或分割视觉管线。
- [ ] GPU 推理后端评估，例如 CUDA、ONNX Runtime 或 TensorRT。

上述功能需要分别建立独立设计文档和 TODO，不直接追加到本文件的实施步骤中。完成 P0/P1 前，它们不作为秋招版本的完成条件。

## 秋招版本完成标准

- [ ] 自动化测试可在本机和 CI 稳定通过。
- [ ] 4/9/16 路性能结果具有机器配置、素材规格和原始日志。
- [ ] YOLO 推理不阻塞播放调度和 UI 响应。
- [ ] Release 包可在干净 Windows 环境启动。
- [ ] README 包含真实运行截图、演示和完整启动步骤。
- [ ] 简历上的每项数字都能对应代码、测试或验收报告。
- [ ] 仓库不再跟踪 IDE 用户配置，不嵌入开发者绝对路径。
- [ ] 已知限制明确写入 README，不将目标指标表述为实测结果。

## 建议执行顺序

1. Phase 1：自动化测试与 CI。
2. Phase 2：量化性能基线。
3. Phase 6.1：README 展示材料，可与 Phase 2 并行准备素材。
4. Phase 3：视觉推理异步化。
5. Phase 4：构建与发布。
6. Phase 5：架构和代码质量收口。
7. Phase 6.2：统一最终简历口径。

## 风险与沟通点

- 自动化测试框架已选择 Qt Test，复用现有 Qt 依赖；后续新增测试继续保持一个模块一个独立测试目标。
- GitHub CI 是否能直接使用仓库内第三方库，需要结合 Git LFS 配额和构建时长确认。
- 异步推理会改变帧所有权和关闭时序，实施前必须先完成 Phase 3.1 设计，不应直接修改线程模型。
- 移除 `TINY_LOG_PROJECT_ROOT` 会影响配置、模型和日志路径，需要统一迁移，不能只修改单个模块。
- `MediaPipeline` 解耦涉及 UI、应用层和渲染设施，必须按小步骤实施并保留每步可运行性。
- 当前工作区已有用户修改的 `CMakeLists.txt.user` 和未跟踪的 `doc/面试复盘.md`；后续修改时需要保留这些现有工作，不覆盖或回退。
