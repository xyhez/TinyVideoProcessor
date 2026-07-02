# TODO: Phase 6 高级图像分析

## 当前进度

- 已完成：
  - Phase 5 图像处理基础链路已完成。
  - active stream 图像处理策略已完成。
  - OpenCV 基础依赖和 `cv::Mat` 转换边界已完成。
  - Phase 6.1 高级图像分析预研和任务拆分已完成。
- 未完成：
  - 检测参数和类型定义。
  - 目标检测处理器。
  - Controller 和 UI 最小接入。
  - Phase 6 验收报告。
- 已知风险：
  - 固定阈值检测对光照和视频内容敏感。
  - 高分辨率视频会放大 OpenCV 处理开销。
  - 第一版只处理 active stream，不承诺多路同时检测。
  - 第一版只做画面可视化，不做检测结果存储。
- 本轮范围：
  - 新增 Phase 6 预研报告。
  - 新增 Phase 6 TODO。
  - 明确第一版使用连通域 / 轮廓检测 + 画框 + 计数。

## 阶段目标

在 Phase 5 图像处理基础上增加第一版高级图像分析能力。

第一版目标是：

```text
active stream
    -> ObjectDetectionProcessor
    -> 目标框
    -> 目标计数
    -> MultiViewRenderer 显示
```

第一版不做：

- 多路同时检测。
- 每路独立检测参数。
- 深度学习模型推理。
- GPU 图像处理。
- 目标跟踪。
- 标定和真实尺寸测量。
- 检测结果存储。
- 统计报表。

## Phase 6.1 高级图像分析预研和任务拆分

**目标**：明确 Phase 6 第一版范围、架构边界、检测方案和任务拆分。

**预计修改文件**：

- `doc/Phase6_高级图像分析预研报告.md`
- `doc/Phase6TODO.md`

**任务清单**：

- [x] 明确第一版只处理 active stream。
- [x] 明确第一版采用连通域 / 轮廓检测。
- [x] 明确第一版直接在处理器内画框和计数。
- [x] 明确不做多路同时检测。
- [x] 明确不做深度学习模型推理。
- [x] 明确不做检测结果存储。
- [x] 拆分后续可执行任务。

**验收标准**：

- [x] 能说明 Phase 6 与 Phase 5 的衔接关系。
- [x] 能说明为什么第一版不新增结果总线。
- [x] 能说明目标检测处理器应继续实现 `IImageProcessor`。
- [x] 能说明 UI 不直接访问检测处理器。
- [x] 能说明后续如何演进到结果记录和统计。

**测试流程**：

- [x] 静态检查：`git diff --check -- doc\Phase6_高级图像分析预研报告.md doc\Phase6TODO.md`
- [x] 定向搜索：`rg -n "Phase 6|ObjectDetectionProcessor|active stream|IImageProcessor|DetectionResult|轮廓|连通域" doc\Phase6_高级图像分析预研报告.md doc\Phase6TODO.md`

**Commit message**：

```text
docs: 增加 Phase 6 高级图像分析规划

- 新增 Phase 6 高级图像分析预研报告
- 新增 Phase 6 TODO
- 明确第一版采用 active stream 目标检测策略
- 明确目标检测以轮廓/连通域画框计数为第一阶段目标
- 明确不做多路同时检测、模型推理和结果存储
```

## Phase 6.2 检测参数和类型定义

**目标**：新增目标检测所需的参数结构，为处理器和 UI 配置打基础。

**预计修改文件**：

- `src/infrastructure/objectdetectionconfig.h`
- `doc/Phase6TODO.md`

**任务清单**：

- [ ] 新增 `ObjectDetectionConfig`。
- [ ] 包含二值化阈值。
- [ ] 包含最小面积。
- [ ] 包含最大面积。
- [ ] 包含是否绘制目标框。
- [ ] 包含是否绘制计数。
- [ ] 给所有成员变量添加 `///<` 注释。
- [ ] 不接入播放链路。

**验收标准**：

- [ ] 参数结构职责单一。
- [ ] 参数默认值适合第一版直接使用。
- [ ] 不引入 UI 依赖。
- [ ] 不引入 FFmpeg 依赖。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\objectdetectionconfig.h doc\Phase6TODO.md`
- [ ] 定向搜索：`rg -n "ObjectDetectionConfig|threshold|minArea|maxArea|drawBoundingBox|drawCount" src\infrastructure doc\Phase6TODO.md`

**Commit message**：

```text
feat: 新增目标检测参数结构

- 新增 ObjectDetectionConfig 保存基础检测参数
- 支持阈值、面积过滤和绘制开关配置
- 为 ObjectDetectionProcessor 和 UI 接入准备稳定参数边界
```

## Phase 6.3 目标检测处理器空壳

**目标**：新增 `ObjectDetectionProcessor`，先接入 `IImageProcessor` 抽象，不实现复杂检测逻辑。

**预计修改文件**：

- `src/infrastructure/objectdetectionprocessor.h`
- `src/infrastructure/objectdetectionprocessor.cpp`
- `src/infrastructure/objectdetectionconfig.h`
- `doc/Phase6TODO.md`

**任务清单**：

- [ ] 新增 `ObjectDetectionProcessor`。
- [ ] 实现 `IImageProcessor`。
- [ ] 支持 `Name()`。
- [ ] 支持 `Enabled()`。
- [ ] 支持 `SetEnabled()`。
- [ ] 支持配置参数传入。
- [ ] `Process()` 第一版可直接返回 true 或做最小占位。
- [ ] 不访问 UI。
- [ ] 不保存帧指针。

**验收标准**：

- [ ] 处理器接口完整。
- [ ] 处理器可被 `ImageProcessorPipeline` 持有。
- [ ] 处理器不破坏现有图像处理链路。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\objectdetectionprocessor.h src\infrastructure\objectdetectionprocessor.cpp src\infrastructure\objectdetectionconfig.h doc\Phase6TODO.md`
- [ ] 定向搜索：`rg -n "ObjectDetectionProcessor|IImageProcessor|Process|Enabled|SetEnabled|ObjectDetectionConfig" src\infrastructure doc\Phase6TODO.md`

**Commit message**：

```text
feat: 新增目标检测处理器空壳

- 新增 ObjectDetectionProcessor 并实现 IImageProcessor
- 支持检测参数保存和启用状态控制
- 为后续轮廓/连通域检测实现准备处理器入口
```

## Phase 6.4 轮廓 / 连通域检测实现

**目标**：实现第一版目标检测算法，在图像上绘制目标框和计数。

**预计修改文件**：

- `src/infrastructure/objectdetectionprocessor.h`
- `src/infrastructure/objectdetectionprocessor.cpp`
- `doc/Phase6TODO.md`

**任务清单**：

- [ ] 将输入 BGR 图像转灰度。
- [ ] 执行基础滤波。
- [ ] 执行二值化。
- [ ] 执行轮廓查找或连通域分析。
- [ ] 按最小面积过滤。
- [ ] 按最大面积过滤。
- [ ] 绘制目标框。
- [ ] 绘制目标数量。
- [ ] 异常输入返回 false。

**验收标准**：

- [ ] 目标明显的视频中能看到目标框。
- [ ] 目标数量显示在画面上。
- [ ] 调整面积阈值能影响检测结果。
- [ ] 输入空图或异常图时不崩溃。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\objectdetectionprocessor.h src\infrastructure\objectdetectionprocessor.cpp doc\Phase6TODO.md`
- [ ] 定向搜索：`rg -n "cvtColor|threshold|findContours|connectedComponents|boundingRect|rectangle|putText|ObjectDetectionProcessor" src\infrastructure doc\Phase6TODO.md`
- [ ] 人工运行建议：普通文件源开启目标检测。
- [ ] 人工运行建议：调节阈值和面积参数确认效果变化。

**Commit message**：

```text
feat: 实现基础目标检测画框计数

- 在 ObjectDetectionProcessor 中实现轮廓/连通域目标提取
- 支持阈值和面积过滤
- 在处理后画面上绘制目标框和目标计数
- 保持检测逻辑封装在图像处理器内部
```

## Phase 6.5 Controller 和 UI 最小接入

**目标**：把目标检测模式接入现有图像处理 UI，允许用户启用和调参。

**预计修改文件**：

- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `doc/Phase6TODO.md`

**任务清单**：

- [ ] 图像处理模式新增目标检测。
- [ ] Controller 保存目标检测配置。
- [ ] Controller 根据模式创建 `ObjectDetectionProcessor`。
- [ ] UI 新增最小面积控件。
- [ ] UI 新增最大面积控件。
- [ ] 目标检测模式下启用阈值和面积控件。
- [ ] 无 active stream 时控件安全禁用。
- [ ] 切换 active stream 后重新应用配置。

**验收标准**：

- [ ] 用户可以选择目标检测模式。
- [ ] 用户可以调整阈值。
- [ ] 用户可以调整最小面积。
- [ ] 用户可以调整最大面积。
- [ ] 多路播放时只影响 active stream。
- [ ] UI 不直接访问 `ObjectDetectionProcessor`。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\application\MediaPlayController.h src\application\MediaPlayController.cpp src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui doc\Phase6TODO.md`
- [ ] 定向搜索：`rg -n "ObjectDetection|ImageProcessingMode|SetImageProcessing|MinArea|MaxArea|MainWindow" src doc\Phase6TODO.md`
- [ ] 人工运行建议：文件源、摄像头源、RTSP 源分别开启目标检测。
- [ ] 人工运行建议：多路切换 active stream 后确认检测只作用于当前 active stream。

**Commit message**：

```text
feat: 接入目标检测最小 UI 控制

- 图像处理模式新增目标检测
- Controller 支持配置 ObjectDetectionProcessor
- 主窗口增加目标检测面积参数控件
- 保持目标检测只作用于 active stream
```

## Phase 6.6 高级图像分析验收

**目标**：确认目标检测不会破坏播放、多路、实时源和生命周期。

**预计修改文件**：

- `doc/Phase6_高级图像分析验收报告.md`
- `doc/Phase6TODO.md`

**任务清单**：

- [ ] 验证文件源目标检测。
- [ ] 验证无音频文件目标检测。
- [ ] 验证摄像头源目标检测。
- [ ] 验证 RTSP 源目标检测。
- [ ] 验证播放、暂停、停止、seek。
- [ ] 验证多路切换 active stream。
- [ ] 验证 remove stream。
- [ ] 验证关闭窗口。
- [ ] 记录基本性能变化。
- [ ] 形成验收报告。

**验收标准**：

- [ ] 目标框和计数可见。
- [ ] 参数调整有效。
- [ ] 关闭检测后恢复原图或基础处理链路。
- [ ] seek 后不显示旧检测结果。
- [ ] remove stream 后不访问已释放帧。
- [ ] 关闭程序不阻塞。

**测试流程**：

- [ ] 静态检查：`git diff --check -- doc\Phase6_高级图像分析验收报告.md doc\Phase6TODO.md`
- [ ] 定向搜索：`rg -n "Phase 6|目标检测|ObjectDetection|active stream|验收|性能" doc\Phase6_高级图像分析验收报告.md doc\Phase6TODO.md`
- [ ] 人工运行建议：普通文件源、摄像头源、RTSP 源。
- [ ] 人工运行建议：多路 2x2 / 3x3，切换 active stream 后检测。

**Commit message**：

```text
docs: 完成 Phase 6 高级图像分析验收

- 增加 Phase 6 高级图像分析验收报告
- 记录目标检测在文件、摄像头和 RTSP 源下的验证结果
- 记录 active stream 目标检测策略验证结果
- 明确后续目标跟踪、标定和数据记录演进方向
```

## 后续演进

- 目标形状识别：圆形、矩形、多边形。
- 颜色目标检测：HSV 范围筛选。
- ROI 检测区域。
- 目标跟踪 ID。
- 像素标定和尺寸测量。
- 检测结果事件通道。
- 检测结果截图。
- SQLite 记录。
- 报表导出。

## 风险与沟通点

- 需要用户提供更适合目标检测的测试素材。
- 固定阈值不适合所有光照条件。
- 如果目标检测要服务工业场景，需要明确目标形态、良品/不良品标准和验收指标。
- 如果要做统计和存储，需要先设计 `DetectionResult` 的跨层传递方式。
