# TinyVideoProcessor 模拟面试 94 问

本文档按 C++、Qt、音视频与工业软件方向秋招面试整理。答案以当前项目实际实现为边界，区分正式能力、实验能力和后续设计。面试时应理解后再用自己的语言回答，不建议逐字背诵。

## 一、项目介绍

### 1. 请用三分钟介绍 TinyVideoProcessor，重点讲目标、架构、核心难点和实际结果。

TinyVideoProcessor 是我基于 C++20、Qt6、FFmpeg、OpenGL 和 OpenCV 独立开发的多源多路视频处理平台。项目最初用于系统学习 FFmpeg 播放链路，后来逐步扩展到本地文件、USB 摄像头和 RTSP，并让这些输入复用同一套解复用、解码、调度、图像处理和渲染链路。

架构分为 UI、Application、Domain 和 Infrastructure 四层。单路播放由 `MediaPipeline` 管理，多路生命周期由 `MultiStreamManager` 管理，视频帧经过 `VideoFrameScheduler` 调度后，可以串联 OpenCV 图像处理、视觉分析和 `MultiViewRenderer` 多视口渲染。

项目中最难的三部分是：第一，跨线程资源生命周期，需要处理阻塞 I/O、队列唤醒、RAII、暂停、停止和自然结束；第二，音视频同步与高帧率调度，需要处理主时钟、等待、丢帧、倍速和累计漂移；第三，多路 OpenGL 渲染，需要处理 slot 映射、格式转换、纹理复用和最新帧合并。

当前已完成文件、摄像头和 RTSP 输入，支持 Single、2x2、3x3、4x4 布局，9 路属于正式定位，16 路属于依赖机器和素材的实验能力；接入了 OpenCV 轮廓检测和 YOLO ONNX 管线，并建立了 5 个 Qt Test 目标、30 个测试用例。自动重连、异步推理和完整性能基准仍是后续工作。

### 2. 这个项目与普通 FFmpeg 播放器相比，核心区别是什么？

普通播放器主要解决单路媒体的解码和播放，本项目还解决多种媒体源统一抽象、多路 Pipeline 生命周期管理、活动视频流和活动音频流分离、统一 OpenGL 多视口渲染、可插拔图像与视觉处理、运行指标和自动化测试。因此它更接近一个可扩展的视频处理框架，而不只是播放器界面。

### 3. 哪些代码是你独立设计的，哪些思路参考了 ffplay 或其他项目？

项目中的业务代码和架构由我独立设计和实现，Qt、FFmpeg、OpenCV 等属于使用的第三方基础库。我参考了 FFmpeg 官方文档、示例和 ffplay 的时钟同步思路，但没有直接复制或封装 ffplay。四层分层、`MediaPipeline`、多路管理、FrameSink、视觉管线、运行指标和资源生命周期是在项目迭代中自行设计的。

### 4. 项目目前完成到什么程度？哪些能力只是实验支持？

文件播放、摄像头 MVP、RTSP、多路管理、四种布局、基础图像处理、OpenCV 和 YOLO 管线已经完成。9 路来自实际播放验收，是当前对外定位的正式能力；16 路虽然进行过实验运行，但性能依赖硬件、分辨率和编码规格，不承诺所有场景稳定满帧。自动重连、异步推理、完整长稳压测、统一性能表和工业业务闭环尚未完成。

### 5. 你认为项目里技术含量最高的三个部分是什么？为什么？

第一是多线程生命周期，因为一个关闭操作会同时影响媒体源、Demuxer、Decoder、队列、AudioPlayer 和线程，顺序错误会造成卡死、尾帧截断或 use-after-free。第二是音视频同步与调度，因为要根据音频时钟决定等待、显示或丢帧，还要解决高帧率取整和处理耗时造成的漂移。第三是多路渲染，因为统一 Renderer 要同时管理多路帧所有权、slot 映射、像素格式、纹理更新和 UI 刷新压力。

## 二、架构设计

### 6. 为什么划分 UI、Application、Domain、Infrastructure 四层？各层职责是什么？

UI 层只负责展示和交互；Application 层把按钮、快捷键等交互转换成播放用例；Domain 层管理媒体源、单路 Pipeline、多路状态和 active stream；Infrastructure 层封装 FFmpeg、Qt Audio、OpenGL、OpenCV 等技术细节。这样 UI 不需要直接操作 Decoder、线程和队列，底层实现变化也不会扩散到整个程序。

### 7. MediaPipeline 负责什么？为什么不把这些逻辑全部放进 MediaPlayController？

`MediaPipeline` 管理一路媒体的 Demuxer、音视频 Decoder、队列、AudioPlayer、Scheduler 和 FrameSink，并负责这条链路的初始化、暂停、停止、Seek、自然结束和释放。`MediaPlayController` 属于应用层，只应该接收 UI 命令并调用领域能力。如果把每路底层状态都堆进 Controller，多路扩展后会产生大量条件分支、交叉资源和难以验证的状态组合。

### 8. MultiStreamManager 与 MediaPipeline 是什么关系？

`MultiStreamManager` 是多个 `MediaPipeline` 的聚合管理者。它保存 `StreamId -> MediaPipeline`，负责添加和移除流、批量启停、活动视频流、活动音频流、视觉分析激活以及流异常后的回退；每个 `MediaPipeline` 仍只处理一路媒体。

### 9. 为什么 StreamId 由多路管理器维护，而不是写进单路管线？

`StreamId` 是多路场景中的业务身份，用于列表、slot 和活动流选择。单路 Pipeline 本身只需要知道如何播放一路媒体，不需要知道自己是第几路。把 `StreamId` 放入 Pipeline 会让可复用的单路组件反向依赖多路业务概念。

### 10. IMediaSource 解决了什么问题？文件、摄像头和 RTSP 如何复用后续链路？

`IMediaSource` 统一了 Open、Close、格式上下文、Seek 能力、实时性、Duration、唯一身份和中断接口。文件、摄像头和 RTSP 主要区别在于如何打开输入；成功打开后都向 Demuxer 提供 `AVFormatContext`，后续 packet、Decoder、Scheduler 和 Renderer 链路就可以复用。

### 11. 如果增加 HTTP-FLV，现有架构需要修改哪些模块？

需要新增或扩展 `IMediaSource` 实现，同时增加 URL 协议识别、打开参数、超时和中断配置、媒体源工厂以及 UI 添加入口。因为输出仍是 `AVFormatContext`，Demuxer、Decoder、Scheduler 和 Renderer 通常不需要修改。不能简单回答只加一个子类，因为来源创建和 UI 入口也需要识别新协议。

### 12. active stream 与 active audio stream 为什么要分开？

active stream 决定进度控制、图像处理和视觉分析针对哪一路；active audio stream 决定哪一路可以发声。监控场景中用户可能查看一路画面但监听另一路声音，因此两者不能强绑定。

### 13. 多路场景中，非活动流是否继续解码音频？目前策略有什么代价？

当前非活动流仍会建立并运行音频解码和消费链，只是在输出侧静音。单路音频开销通常低于视频，但多路累计后仍会消耗解码、重采样和音频输出资源。进一步优化时可以只为活动音频流启用完整音频消费链。

### 14. 当前最大的架构债务是什么？为什么暂时没有重构？

当前主要债务是 `MainWindow` 职责仍然偏多，视觉推理仍位于同步显示路径，部分构建和资源路径也依赖开发环境。现在不做全面重构，是因为秋招版本更需要稳定、可演示、文档和面试准备；大范围修改会增加回归成本。视觉异步化和 UI 继续拆分应作为独立后续 Phase。

## 三、线程与生命周期

### 15. 一路播放包含哪些线程？每个线程分别执行什么任务？

一路通常包含 Demuxer 线程、视频 Decoder 线程、可选音频 Decoder 线程和 AudioPlayer 线程；`VideoFrameScheduler` 与 `MultiViewRenderer` 位于 UI 线程。Demuxer 生产 packet，Decoder 生产 frame，AudioPlayer 消费音频帧并更新音频时钟，Scheduler 决定视频帧等待、丢弃或显示，Renderer 上传纹理并绘制。

### 16. Qt 的 moveToThread() 改变了什么？对象成员函数一定会在目标线程执行吗？

`moveToThread()` 改变 QObject 的线程亲和性，不会改变普通 C++ 函数调用的执行线程。直接调用对象成员函数仍在调用者线程执行；排队信号、事件和 queued `invokeMethod` 才会在接收对象所属线程执行。

### 17. DirectConnection、QueuedConnection 和 AutoConnection 在项目中分别有什么影响？

DirectConnection 在发射信号的当前线程同步执行槽，调用者要等槽返回；跨线程直接修改接收对象状态存在风险。QueuedConnection 把调用封装成事件投递到接收对象所属线程，发送方立即返回。AutoConnection 在信号发射时比较当前执行线程与接收对象的线程亲和性，决定使用直接还是排队连接，不能简单理解成比较发送对象和接收对象的归属线程。

### 18. 关闭一路视频时，资源释放顺序是什么？为什么顺序不能随意调整？

先停止位置定时器并请求媒体源中断，再停止队列和工作对象并唤醒阻塞线程；随后让各 QThread `quit()`、`wait()`；确认线程退出后销毁 Demuxer、Decoder、AudioPlayer、Sink 和队列，最后关闭媒体源。若先释放对象再等待线程，工作线程可能访问已释放内存；若只调用 `quit()` 而不打断 I/O，则可能永久等待。

### 19. RTSP 或摄像头阻塞在 av_read_frame() 时，如何保证关闭操作不会永久卡住？

在实时源的 `AVFormatContext` 中安装 interrupt callback，回调只读取一个原子中断标志。关闭或暂停时设置标志，使 FFmpeg 的阻塞 I/O 提前返回；Demuxer 再检查 Stop 或 Pause 状态，决定退出还是进入可恢复等待。

### 20. Pause、Stop、Shutdown 在语义上有什么区别？

Pause 是保留资源和当前位置的可恢复暂停；Stop 清空当前播放状态并回到起点，实时源则重置运行状态；Shutdown 停止线程并彻底释放整条 Pipeline，不能直接恢复。

### 21. 如果线程仍在使用对象，主线程提前析构对象会发生什么？

会产生 use-after-free、数据竞争和未定义行为，表现可能是立即崩溃、随机崩溃或静默数据破坏，而不只是简单的内存越界。因此必须先通知停止、解除阻塞并等待线程退出，再释放对象。

### 22. QThread::quit() 为什么不一定能停止正在执行的阻塞函数？

`quit()` 只是请求退出 QThread 的事件循环。如果线程正在普通长函数、死循环或 `av_read_frame()` 中阻塞，事件循环没有机会处理退出请求，因此必须配合业务停止标志、队列唤醒和底层 I/O interrupt callback。

## 四、队列与 C++ 所有权

### 23. 为什么将 AVPacket* 和 AVFrame* 改成 unique_ptr + 自定义 deleter？

FFmpeg 对象必须使用 `av_packet_free()` 和 `av_frame_free()` 释放，裸指针无法清晰表达谁负责释放。`unique_ptr + 自定义 deleter` 可以在异常、提前返回和队列清理时自动释放，并通过 move 明确表示 packet/frame 所有权转移。

### 24. av_frame_ref()、av_frame_clone()、av_frame_unref() 和 av_frame_free() 有什么区别？

`av_frame_ref()` 让目标 Frame 引用源 Frame 的引用计数缓冲；`av_frame_clone()` 创建新的 `AVFrame` 对象，但底层像素或音频数据通常仍通过引用计数共享，不是深拷贝；`av_frame_unref()` 释放内部缓冲引用并重置字段，但保留 Frame 对象；`av_frame_free()` 先 unref，再释放 Frame 对象并将指针置空。真正深拷贝需要重新分配缓冲并调用 `av_frame_copy()`、`av_frame_copy_props()` 等接口。

### 25. 为什么队列元素适合使用 unique_ptr，而队列对象使用 shared_ptr？

队列会同时被生产者、消费者和 Pipeline 持有，因此其生命周期是共享的；每个 packet/frame 在任意时刻只应有一个逻辑所有者，因此元素使用 `unique_ptr`，入队和出队都通过 move 转移所有权。

### 26. SyncQueue 为什么需要区分 Item、Interrupted、Stopped 和 EndOfStream？

Item 表示成功取得数据；Interrupted 表示 Seek 或 Pause 造成的可恢复唤醒；Stopped 表示不可恢复取消；EndOfStream 表示生产结束，但队列已有数据仍要排空。如果统一返回 bool，消费者无法判断应该继续等待、退出还是执行 drain。

### 27. Stop 和 EndOfStream 的行为为什么不能合并？

Stop 是取消语义，应立即结束并允许丢弃剩余数据；EOS 是正常完成语义，必须继续排空 packet 队列、Decoder 内部缓存、frame 队列、重采样器和设备缓冲。二者合并会导致自然播放结束时尾帧被截断。

### 28. 队列到达 EOS 后，为什么还要允许消费者排空已有元素？

EOS 只表示生产者不会再产生新元素，不代表队列已经为空。消费者必须先取完 EOS 之前已入队的数据，只有队列为空且带 EOS 状态时，才能宣告该阶段真正结束。

### 29. 队列同时使用原子变量和互斥锁是否合理？分别保护什么？

合理，但职责必须明确。原子变量适合可独立读取的 Stop 或 Interrupt 快速标志；队列容器、多个关联状态、条件变量谓词和检查后修改等复合操作必须由同一 mutex 保护。atomic 不能维护一组状态之间的不变量。

### 30. 如何证明 Seek、Stop 和 Shutdown 不会造成重复释放或旧帧残留？

测试不能数学上证明完全没有问题，只能提高可信度。我通过队列和调度单元测试、重复 Seek/Stop、移除流、播放中关闭、多路关闭和日志顺序验证；还应增加 ASan、固定次数循环测试、线程退出超时和长时间压力测试。旧帧方面需要检查 Seek 后目标时间之前的帧被丢弃、Renderer 清帧且队列被重置。

## 五、FFmpeg 与播放链路

### 31. 从 av_read_frame() 到画面显示，一帧视频依次经过哪些模块？

完整链路是：`IMediaSource -> Demuxer/av_read_frame -> AVPacket 队列 -> Decoder -> AVFrame 队列 -> VideoFrameScheduler -> ImageProcessingFrameSink -> VisionProcessingFrameSink -> MultiViewRenderer`。未启用图像或视觉处理时，对应 Sink 直接透传原帧。

### 32. 为什么 demuxer 读到 EOF 后不能立即停止 decoder 和帧队列？

EOF 只表示 Demuxer 不会再读到新 packet。packet 队列可能还有数据，Decoder 内部也可能保留重排序帧；立即 Stop 会截断 B 帧、音频尾帧和消费者队列。因此 Demuxer 应发送 packet EOS，Decoder 排空后再发送 frame EOS。

### 33. B 帧为什么可能在 packet 读取结束后仍留在解码器内部？

B 帧的显示顺序和解码顺序不同，并且依赖前后参考帧。Decoder 为了按正确 PTS 顺序输出，会缓存一部分已经解码但暂时不能输出的帧。因此最后一个 packet 输入后，内部仍可能保留等待输出的 B 帧。

### 34. 如何通过向解码器发送空 packet 完成 drain？

packet 队列排空并收到 EOS 后，调用 `avcodec_send_packet(codecContext, nullptr)` 通知 Decoder 输入结束，再循环调用 `avcodec_receive_frame()` 输出剩余帧，直到返回 `AVERROR_EOF`。若发送空 packet 时返回 EAGAIN，应先继续 receive，不能直接认为 drain 结束。

### 35. Seek 的完整过程是什么？为什么必须清队列并调用 avcodec_flush_buffers()？

先记录原播放状态并暂停 Scheduler、AudioPlayer、Demuxer 和 Decoder；清理当前显示帧和 packet/frame 队列；调用 `av_seek_frame()` 定位关键帧；成功后执行 `avcodec_flush_buffers()`，清除 Decoder 中属于旧时间线的参考帧和重排状态；重置队列的 Stop/EOS、同步时钟和完成状态；设置目标时间前丢帧，最后恢复到 Seek 前的播放或暂停状态。

### 36. Seek 为什么通常只能先定位到关键帧？如何处理目标时间之前的帧？

P/B 帧依赖参考帧，通常不能从任意帧独立开始解码，所以 Seek 先定位到目标附近的关键帧，再向前解码。Decoder 根据帧 PTS 丢弃目标时间之前的帧，直到达到目标位置后再交给 Scheduler。

### 37. 如果管线暂停后 av_seek_frame() 失败，应该如何恢复状态？

Seek 前应记录 wasPlaying、原位置和队列状态。失败后要恢复队列、工作线程和原时钟，并向上层报告错误；另一种方案是原子地转入明确的 Stopped/Error 状态。不能让 UI 认为仍在播放，而工作线程停留在 suspended 的半状态。

### 38. 没有音频流时用什么作为主时钟？

使用视频时钟。Scheduler 按源帧率或安全默认间隔建立 cadence，在显示帧时用视频 PTS 刷新 `SyncManager`，不再等待不存在的音频时钟。

### 39. 音频流结束但视频还有尾帧时，怎样避免视频一直等待不再前进的音频时钟？

AudioPlayer 排空音频帧、重采样尾部和设备缓冲后发出 drained 信号，Pipeline 将同步类型从 AUDIO 切换为 VIDEO，使剩余视频尾帧按视频 cadence 继续显示，直到视频队列排空。

### 40. 硬件解码失败后如何回退软件解码？硬件帧为什么通常还需要下载或转换？

硬件设备或硬件 Codec 初始化失败时，释放硬件上下文，再重新创建软件 Decoder。硬件帧通常位于 GPU 或专用设备内存，而当前 OpenCV 和部分软件渲染路径需要 CPU 可访问数据，因此要通过 `av_hwframe_transfer_data()` 下载或转换成软件帧。支持硬解不代表已经实现全链路零拷贝。

## 六、同步与调度

### 41. VideoFrameScheduler 为什么需要从渲染器中独立出来？

Renderer 应只负责显示，Scheduler 负责取帧、比较时钟、等待、丢帧、倍速和预览降帧。拆分后同步算法可以独立测试，也能在 Scheduler 和 Renderer 之间插入图像处理与视觉分析，而不修改 OpenGL 模块。

### 42. 音视频同步时，什么情况下等待视频帧，什么情况下丢弃视频帧？

计算 `videoPts - audioClock`。差值超过正阈值表示视频领先，目标显示时间还没到，应有限等待；低于负阈值表示视频落后，应丢帧追赶；位于阈值范围内则显示。

### 43. 为什么使用固定整数帧间隔会在高帧率视频中产生累计漂移？

120 FPS 理论间隔约为 8.333ms，如果每帧都取整为 8ms，每帧少 0.333ms，长期会累计提前。另一方面，如果每次处理完成后再等待完整间隔，格式转换和绘制耗时会被重复叠加，播放又会逐渐变慢。

### 44. 绝对 deadline 调度相比“每次处理完再等待 33ms”有什么优势？

绝对 deadline 从同一单调时间轴持续累加目标时刻，本轮处理或 Timer 超调造成的延迟可以在后续帧中补偿。处理完再等待固定 33ms 会把本轮处理耗时叠加到帧间隔，误差持续累积。

### 45. 120fps 和 240fps 下，毫秒级 Qt Timer 存在哪些限制？

Windows 调度粒度、线程抢占和 UI 负载会影响 Timer 唤醒。240 FPS 每帧预算只有约 4.17ms，毫秒级 Timer 无法保证微秒级精度，因此只能做软实时预览，不能承诺硬实时满帧。

### 46. 播放速度切换后，调度 deadline 为什么需要重置？

倍速改变后目标帧间隔发生变化。如果继续沿用基于旧速度累计的 deadline，会出现短时间突发显示或长时间等待。因此要清除旧 cadence 基线，从当前时刻按新速度重新锚定。

### 47. “调度丢帧率”和“网络丢包率”有什么区别？

调度丢帧是播放器已经成功解码出帧，但因为视频落后或预览限帧而主动不显示；网络丢包发生在传输阶段，数据没有到达，可能引发解码错误或画面损坏。当前项目展示的是调度丢帧，不能称为网络丢包率。

### 48. 预览降帧为什么不能简单地每隔 N 帧显示一次？

每 N 帧取一帧假设输入帧率固定；可变帧率、倍速和异常 PTS 下，实际预览 FPS 会波动。依据相邻显示帧的 PTS 时间差判断，才能在时间维度上维持目标预览帧率。

## 七、OpenGL 渲染

### 49. 为什么使用统一 MultiViewRenderer，而不是每路创建一个 QOpenGLWidget？

一个 `MultiViewRenderer` 只需维护一个 OpenGL 上下文、Shader 和绘制循环，所有 slot 可以统一布局和刷新。每路一个 `QOpenGLWidget` 会增加上下文切换、资源管理、事件叠加、高 DPI 布局和多控件刷新成本。

### 50. StreamId -> slot -> viewport 的映射如何工作？

`MultiStreamManager` 为流分配逻辑 slot，Renderer 保存 `slot -> StreamId` 映射。当前布局决定可见 slot 数量，再将每个 slot 计算成 Qt 逻辑矩形和 OpenGL framebuffer viewport。切换 Single、2x2、3x3、4x4 只改变可见范围和几何计算，不改变流本身。

### 51. YUV420P 有几个平面？每个平面的尺寸和作用是什么？

YUV420P 包含 Y、U、V 三个平面。Y 是全分辨率亮度平面；U、V 是色度平面，宽和高通常各为 Y 的一半。偶数宽高时，一帧总数据量约为 `width * height * 1.5`。

### 52. NV12 与 YUV420P 有什么区别？为什么 NV12 可以减少一次格式转换？

YUV420P 的 U、V 分开存储，NV12 则是一个 Y 平面加一个 U、V 交错的 UV 平面。硬件解码常输出 NV12，Renderer 可以把 Y 上传为单通道纹理，把 UV 上传为 RG 双通道纹理，直接在 Shader 中转换颜色，从而避免 NV12 到 YUV420P 的 CPU 转换。

### 53. FFmpeg 的 linesize 为什么可能大于图像宽度？

`linesize` 表示一行数据占用的字节数，不一定等于像素宽度。Decoder 和 SIMD 为了内存对齐会增加行尾 padding，不同像素格式每像素字节数也不同。纹理上传时必须按 stride 逐行处理或设置正确 row length，不能假设缓冲紧密排列。

### 54. 为什么 OpenGL 纹理只在尺寸或格式变化时重建？

纹理创建和重新分配会触发显存分配、驱动状态变化和同步开销。分辨率和平面格式不变时应复用纹理，只更新像素数据；只有宽高、像素格式或平面结构变化时才需要重建。

### 55. “只保留最新待显示帧”解决了什么问题？会丢失什么信息？

当生产速度高于 OpenGL 绘制速度时，若每帧排队会导致内存增长和预览延迟不断扩大。最新帧策略让新帧覆盖尚未绘制的 pending 帧，优先保证实时性。代价是中间预览帧不会逐帧显示，因此不适用于录像、逐帧存档等不可丢帧任务。

### 56. 为什么 OpenGL 纹理必须在拥有有效上下文的线程中创建和销毁？

OpenGL 资源属于上下文或上下文共享组，创建、上传和销毁时必须有对应上下文在当前线程处于 current 状态。在错误线程或无上下文状态下操作可能产生 GL 错误、资源泄漏或未定义行为。

### 57. 当前颜色转换是否完整处理了色彩空间和 limited/full range？

没有完全处理。当前 NV12 路径会根据部分颜色范围和色彩空间选择转换参数，但 YUV420P Shader 仍使用固定转换系数，对 BT.601、BT.709、BT.2020、limited/full range 和 HDR 没有完整统一处理。因此只能称为基础颜色转换，不能宣传为完整色彩管理。

## 八、网络流与摄像头

### 58. RTSP 输入与本地文件输入在 Seek、Duration 和实时性语义上有什么区别？

本地文件通常可 Seek、有确定 Duration，读取速度由播放器消费速度决定；RTSP 是持续到达的实时输入，当前按不可 Seek、无确定 Duration、实时源处理。实时源还必须处理打开超时、读取超时、断流和阻塞 I/O 中断，不能使用文件自然结束后的回放语义。

### 59. FFmpeg interrupt callback 如何打断阻塞 I/O？回调里为什么不能执行复杂逻辑？

在 `AVFormatContext` 中设置 `interrupt_callback` 和 `opaque`，回调通过 `opaque` 找到媒体源对象并读取原子中断标志。关闭或暂停时设置该标志，FFmpeg 在网络或设备 I/O 等待期间调用回调；回调返回非零后，`avformat_open_input()`、`av_read_frame()` 等阻塞操作提前返回。

回调可能在 FFmpeg 内部线程和锁环境中高频执行，因此只能进行快速、无阻塞、线程安全的判断。不能在其中更新 UI、等待互斥锁、销毁 `AVFormatContext` 或执行复杂业务，否则可能造成死锁、递归释放或明显性能问题。

### 60. 当前所谓“断流处理”具体做了什么？是否支持自动重连？

当前网络源设置了 RTSP TCP 传输、打开和读取超时、缓冲及低延迟参数，并安装 interrupt callback。Demuxer 对读取错误进行有限重试，连续失败达到上限后发出错误信号，停止队列和当前播放链；移除流或关闭程序时能够主动中断阻塞读取。

当前不支持自动重连。服务恢复后需要用户重新添加流，因此面试时应表述为“支持断流检测、错误上报和可中断关闭”，不能说“已完成自动重连”。

### 61. 如果实现自动重连，你如何设计退避、重试上限和状态恢复？

我会建立 `Connecting -> Playing -> Reconnecting -> Failed/Stopped` 状态机。断流后先安全停止并销毁旧 Pipeline，再按 1、2、4、8、16 秒指数退避，并加入少量随机抖动，最大间隔限制在 30 秒左右；同时设置最大重试次数或总重试时长，避免永久占用资源。

每个重连任务都带取消标志和 generation。用户删除流、关闭程序或重新添加同一 URL 后，旧 generation 的任务即使返回也不能恢复旧 Pipeline。重连成功后复用原 `StreamId`、slot、视觉配置和音频选择，重置队列、时钟和运行指标，确认已获得有效视频流后再切换到 Playing。

### 62. 摄像头为什么通过 avdevice 接入？Windows 下使用什么输入设备后端？

普通 `libavformat` 主要处理文件和网络协议，摄像头属于操作系统采集设备，需要 `libavdevice` 将设备输入封装成 FFmpeg 的 `AVFormatContext` 和 `AVStream`。Windows 下当前使用 DirectShow，通过 `av_find_input_format("dshow")` 获取输入格式，设备地址采用 `video=设备名`，并传入分辨率、帧率、像素格式等参数。

### 63. 摄像头暂停后恢复，是否应该复用原连接还是重新打开设备？

短时间 Pause 应优先复用原连接，避免重复打开独占设备带来的延迟、黑屏和打开失败。当前设计通过中断阻塞读取、暂停工作循环，恢复时清除中断标志并继续使用原设备。

如果设备被拔出、驱动进入错误状态、读取连续失败或长时间暂停后句柄失效，则应关闭旧 `AVFormatContext`，重新枚举设备并打开。也就是说，正常暂停走轻量恢复，异常恢复才重建连接。

## 九、OpenCV 与 YOLO

### 64. 图像处理链和视觉分析链为什么分成两套抽象？

图像处理链直接改变像素，例如灰度、二值化和边缘检测；视觉分析链输出检测框、类别、置信度等结构化结果，是否把结果画到画面只是展示策略。分开后，基础图像变换可以自由组合，分析结果也可以在未来用于报警、存储或统计，而不必强制修改视频画面。

### 65. ImageProcessingFrameSink 和 VisionProcessingFrameSink 为什么适合使用装饰器式串联？

两者都实现 `IVideoFrameSink`，接收一帧、可选处理，再把帧所有权转移给下游。Scheduler 只依赖统一 Sink 接口，不需要知道后面是否启用了 OpenCV、YOLO 或 OpenGL。新增处理步骤时只需要增加一个装饰 Sink 并调整组装顺序，不必修改调度器和渲染器。

### 66. FFmpeg AVFrame 如何转换为 cv::Mat？这里有哪些内存复制？

当前使用 `sws_getCachedContext()` 和 `sws_scale()` 将软件 `AVFrame` 的源像素格式转换成 BGR24，并直接写入新分配的 `cv::Mat`，这里发生一次格式转换和像素复制。绘制 overlay 后，再分配 BGR24 `AVFrame`，通过逐行 `memcpy` 把 Mat 数据复制回 Frame，这是第二次复制。

YOLO 管线内部还会对输入 Mat 执行 `clone()`，Letterbox、颜色转换和 DNN blob 构建也可能产生额外缓冲。因此当前视觉链不是零拷贝，硬件帧进入该链之前还需要先下载为软件帧。

### 67. YOLO ONNX 当前为什么可能阻塞播放？

`VisionProcessingFrameSink` 由 UI 线程中的 `VideoFrameScheduler` 同步调用，YOLO 使用 OpenCV DNN CPU 后端执行推理。`net.forward()` 返回前，Scheduler 无法把当前帧继续交给 Renderer，UI 事件和其他流的绘制也会受到延迟。因此模型越大、输入尺寸越高，播放卡顿越明显。

### 68. 如果异步化推理，如何设计有界推理队列、降帧和结果缓存？

显示线程按配置的推理 FPS 向容量为 1 或 2 的最新帧队列提交任务；队列满时覆盖旧帧，不阻塞播放。独立推理线程消费最新帧，执行预处理、推理和后处理。结果缓存使用 `StreamId + PTS + generation + 时间戳` 标识，Renderer 读取最近且未过期的结果。

Seek、切换 active stream、移除流或切换模型时递增 generation，使旧任务和旧结果自动失效。这样显示 FPS、推理 FPS 和结果刷新频率可以解耦，同时避免无界队列让结果越来越滞后。

### 69. Letterbox 会如何影响检测框坐标？怎样映射回原图？

Letterbox 按统一比例缩放原图，并在上下或左右增加 padding，因此模型输出坐标属于“缩放后加填充”的输入坐标系。映射回原图时先减去 padding，再除以缩放比例，最后裁剪到原图边界：

```text
xOriginal = (xModel - padX) / scale
yOriginal = (yModel - padY) / scale
```

框的宽和高同样除以 `scale`。若跳过减 padding，检测框会整体偏移；若 X、Y 使用不同缩放比，又会破坏 Letterbox 的等比例语义。

### 70. confidence threshold 和 NMS threshold 分别控制什么？

confidence threshold 用于过滤置信度过低的候选框，设得高会减少误检但增加漏检。NMS threshold 是候选框之间的 IoU 阈值，用于抑制同一目标的重复框；设得过低可能误删相邻目标，设得过高会留下较多重叠框。两者控制的是不同阶段，不能混用。

### 71. 切换视觉管线时，旧模型和新模型的生命周期如何管理？

当前 Manager 同一时刻只激活一条 Pipeline。切换时先对旧 Pipeline 执行 `Shutdown()`，释放模型和中间资源，再对目标 Pipeline 执行 `Startup()`；成功后更新 active ID。目标启动失败时 active ID 为空，并发出失败信号。

这种策略避免两套模型同时占用大量 CPU/GPU 内存，但失败时无法无缝回退旧模型。若资源允许，可以采用事务式切换：先启动新模型，成功后再关闭旧模型；代价是切换瞬间要同时容纳两套资源。

### 72. JSON 配置不存在或非法时，为什么需要默认配置回退？

视觉配置是可选外部输入，缺失、损坏或字段错误不应导致整个播放器无法启动。Loader 在文件不存在、无法打开、JSON 非法、缺少 pipelines 数组或没有有效任务时回退内置 OpenCV 和 YOLO 配置，并记录警告。这样既保证最小可用能力，也避免每个上层调用方重复处理配置异常。

## 十、测试与工程能力

### 73. 当前五个测试目标分别覆盖什么？

`test_sync_queue` 覆盖顺序、容量阻塞、中断、Stop/Resume、状态优先级和 EOS 排空；`test_video_frame_scheduler` 覆盖帧间隔、非法帧率、处理耗时、Timer 超调、绝对 deadline、重启和 EOS；`test_playlist_manager` 覆盖增删、去重、索引修正、播放模式和信号；`test_vision_config_loader` 覆盖配置缺失、非法 JSON、默认参数合并、重复 ID 和路径；`test_vision_pipeline_manager` 覆盖注册、切换、生命周期、启动失败和未知 ID。

### 74. 为什么队列并发测试不能只验证返回值，还要设置超时？

并发错误可能让函数永远不返回。若只在返回后断言结果，死锁、漏唤醒和永久等待会让测试进程一直卡住，测试框架无法报告失败。设置超时后，线程没有在规定时间完成本身就是失败，从而把系统的活性也纳入验证。

### 75. 当前测试没有覆盖哪些高风险模块？

尚未自动覆盖真实 Demuxer、Decoder、AudioPlayer 完整链路、Seek 集成、B 帧和 AAC 尾部排空、OpenGL 上下文、真实摄像头、RTSP 断流、YOLO 模型推理，以及 9/16 路长期压力、资源泄漏和干净机器部署。这些模块依赖外部设备、网络、GPU、声卡或真实媒体，测试成本更高，但也是后续最有价值的覆盖方向。

### 76. 如何测试 Demuxer、Decoder 和 AudioPlayer 的完整 EOS 链？

准备固定的短视频 fixture，包含 B 帧视频和 AAC 音频，并提前记录预期帧数、最后 PTS 和时长。运行完整链路后验证：Demuxer 只发送 packet EOS，不立即 Stop；Decoder 在 packet EOS 后执行 codec drain，输出预期尾帧并发送 frame EOS；AudioPlayer 排空重采样器和设备缓冲；`PlaybackFinished` 只触发一次；所有工作线程在超时内退出。

为了让 AudioPlayer 测试可重复，应抽象音频输出接口并注入 Fake Audio Sink，模拟可写字节数、设备空闲和排空完成，避免测试依赖真实声卡状态。同时补充无音频视频、只有音频以及中途 Stop 的对照用例。

### 77. 如何构造可重复的 RTSP 断流测试？

在本机运行固定版本的 MediaMTX 等 RTSP 服务端，用 FFmpeg 循环推送固定素材。测试先确认连接成功并持续出帧，再在固定时间终止推流进程或服务端，验证读取超时、错误信号、队列停止和关闭耗时；随后重启服务测试手动重新添加，未来自动重连完成后再验证状态恢复。

测试应固定端口、传输方式、素材、超时参数和操作时刻，并确保服务进程在测试结束后可靠清理。这样可以避免依赖不稳定的公网 RTSP 地址。

### 78. 你所说的 9 路正式支持和 16 路实验支持，有哪些数据证据？

9 路来自 Phase 3.15 的实际解码和多视口渲染验收，16 路也进行过实验运行，但性能明显依赖机器配置和视频规格，因此只定义为实验能力。当前最直观、可展示的证据是 4 路本地视频加视觉分析的运行截图，合计预览提交速率约 55.7 FPS。

现有证据仍缺固定硬件、统一素材、CPU/内存/延迟原始日志和 16 路长时间稳定性测试。因此面试时不能说“任意 16 路稳定满帧”，更严谨的表述是“9 路完成实际播放验收，16 路在当前机器上进行过实验验证”。

### 79. FPS、CPU、内存和延迟应该怎样采集，测试素材应如何固定？

固定 CPU、GPU、内存、系统版本、电源模式和构建类型；固定素材的 Codec、分辨率、帧率、码率、时长和是否含音频。每轮预热 30 秒后采样至少 5 分钟，重复 3 次。

FPS 用提交帧计数增量除以采样时间；CPU 和内存使用 Windows 性能计数器、Process Explorer 或等价工具；GPU 使用厂商工具；内部延迟记录帧进入 Scheduler 到提交 Renderer 的时间，输出平均值、P95 和最大值。真正端到端延迟需要摄像头拍摄统一时钟或 LED 等外部基准，不能用内部处理耗时冒充。

### 80. 为什么 CMakeLists.txt.user 不应该进入仓库？

它保存 Qt Creator Kit、构建目录、绝对路径和本机调试配置，其他开发者无法复用，而且 IDE 会频繁自动修改，造成无意义 diff。文件还可能暴露用户名和本地目录。应加入 `.gitignore` 并使用 `git rm --cached CMakeLists.txt.user` 停止跟踪，但保留本机文件。

### 81. 如何让项目在一台没有 Qt Creator 的干净 Windows 机器上运行？

使用命令行 CMake 配置并构建 Release，不能依赖 Qt Creator 的 user 配置；运行 `windeployqt` 部署 Qt DLL 和平台插件，再复制 FFmpeg、OpenCV 运行库、配置文件和可选 YOLO 模型，并确保目标机有匹配的 VC Runtime。

最后在干净虚拟机中验证程序启动、本地文件播放、配置加载以及缺失 YOLO 模型时的降级行为，并把完整构建、打包、目录结构和运行命令写进 README。

## 十一、C++ 追问与现场题

### 82. unique_ptr 自定义 deleter 会如何影响指针类型和对象大小？

deleter 是 `std::unique_ptr<T, Deleter>` 类型的一部分，因此使用不同 deleter 的 unique_ptr 是不同类型。对象通常需要保存一个指针和 deleter 状态；无状态 deleter 通常可以利用空基类优化，不额外增加大小，函数指针 deleter 通常增加一个指针大小，`std::function` deleter开销更大。项目中使用具名无状态 deleter，可以获得明确类型和较低开销。

### 83. shared_ptr 的控制块包含什么？项目中是否存在循环引用风险？

控制块通常包含强引用计数、弱引用计数、deleter 和 allocator；使用 `make_shared` 时，对象与控制块可能一次分配。当前队列由 Pipeline 和工作对象共享，但工作对象没有通过 `shared_ptr` 反向持有 Pipeline，因此没有明显循环。

如果未来异步回调捕获 `shared_ptr<MediaPipeline>`，同时 Pipeline 又拥有回调来源，就可能形成环。此时应捕获 `weak_ptr`，执行前 `lock()`，或者继续使用明确的父子所有权和借用指针。

### 84. std::atomic<bool> 能否代替互斥锁保护一组状态？

不能。atomic 只能保证单个变量的读写不可分割，无法保证多个字段之间的一致性，也不能保护容器迭代、检查后修改和状态迁移等复合操作。一组状态应在同一 mutex 下读取和更新；atomic 更适合独立的 Stop、Interrupt 或 Dirty 标志。

### 85. 条件变量为什么必须配合谓词循环使用？

条件变量允许虚假唤醒，而且通知发生后，其他线程可能先取得锁并再次改变状态。因此线程被唤醒后必须在同一 mutex 保护下重新检查真实条件，通知本身不代表条件一定成立：

```cpp
cv.wait(lock, [&] {
    return !queue.empty() || stopped || endOfStream;
});
```

### 86. 写一个容量固定、支持 Stop 和 EOS 的阻塞队列。

```cpp
enum class PopResult { Item, Stopped, EndOfStream };

template<class T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity) : capacity_(capacity) {}

    bool Push(T value) {
        std::unique_lock lock(mutex_);
        notFull_.wait(lock, [&] {
            return queue_.size() < capacity_ || stopped_ || eos_;
        });
        if (stopped_ || eos_) return false;

        queue_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    PopResult Pop(T& value) {
        std::unique_lock lock(mutex_);
        notEmpty_.wait(lock, [&] {
            return !queue_.empty() || stopped_ || eos_;
        });

        if (stopped_) return PopResult::Stopped;
        if (queue_.empty()) return PopResult::EndOfStream;

        value = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return PopResult::Item;
    }

    void Stop() {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void SignalEndOfStream() {
        {
            std::lock_guard lock(mutex_);
            eos_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

private:
    std::size_t capacity_;
    std::deque<T> queue_;
    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    bool stopped_ = false;
    bool eos_ = false;
};
```

Stop 的优先级高于队列已有元素，表示立即取消；EOS 时仍先返回已有元素，队列排空后才返回 EndOfStream。

### 87. 设计一个“生产者可以持续提交，但消费者只取最新帧”的线程安全结构。

```cpp
template<class T>
class LatestValue {
public:
    void Submit(T value) {
        {
            std::lock_guard lock(mutex_);
            latest_ = std::move(value); // 覆盖尚未消费的旧值
        }
        ready_.notify_one();
    }

    bool WaitTake(T& value) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [&] { return latest_.has_value() || stopped_; });
        if (stopped_) return false;

        value = std::move(*latest_);
        latest_.reset();
        return true;
    }

    void Stop() {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            latest_.reset();
        }
        ready_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<T> latest_;
    bool stopped_ = false;
};
```

这种结构保证内存不会随生产速度增长，适合实时预览和推理任务；它会主动丢弃中间值，不适用于录像或逐帧存档。

### 88. 给定一组视频帧 PTS 和音频时钟，输出每帧应该等待、显示还是丢弃。

先将视频 PTS 按 time base 转换为毫秒，再计算 `diff = videoPtsMs - audioClockMs`：

```cpp
Decision Decide(double videoPtsMs, double audioClockMs) {
    const double diffMs = videoPtsMs - audioClockMs;
    constexpr double thresholdMs = 40.0;

    if (diffMs > thresholdMs)
        return Decision::Wait;
    if (diffMs < -thresholdMs)
        return Decision::Drop;
    return Decision::Display;
}
```

视频领先表示显示时间还没到，应有限等待；视频落后过多则丢帧追赶；位于阈值内直接显示。实际实现中等待时间还应设置上限，避免一次等待过久。PTS 无效或没有音频时，应回退到视频 cadence 调度。

### 89. 如果程序偶发关闭卡死，你会怎样定位：日志、线程栈和最小复现分别怎么做？

日志方面记录 `StreamId`、线程 ID、状态迁移、队列大小，以及进入和退出 interrupt、Stop、quit、wait 的时间，从最后一条 begin 日志判断卡在哪个阶段。线程栈方面，在卡死时暂停所有线程，检查是否停在 `av_read_frame()`、条件变量、`QThread::wait()`、音频写入或互斥锁，并分析锁顺序。

最小复现方面，固定一个短视频，先只保留单路，循环执行打开、播放、Seek、移除和关闭，再逐步加入音频、RTSP 和多路；每轮设置退出超时。偶发问题可使用 ProcDump 等工具保存卡死现场，不能只依赖正常运行后的日志。

## 十二、收尾问题

### 90. 项目中最难定位的一次 bug 是什么？根因和验证方法是什么？

最有代表性的是高帧率视频调度异常。120 FPS 曾被帧率边界判断当作异常值，回退到 33ms；同时旧调度方式在处理完一帧后再等待完整帧间隔，把格式转换和 UI 处理耗时重复叠加，造成实际 FPS 下降和时钟累计漂移。

修复时我将调度改为单调时钟加绝对 deadline，并从剩余帧预算中扣除本轮处理耗时；严重落后时重新锚定。随后补充了 120/240 FPS、处理耗时、Timer 超调、严重延迟、Stop/Start 重启和 EOS 测试，并通过运行指标观察预览提交 FPS 和调度丢帧率。

### 91. 哪个设计后来证明不合理？你是如何演进它的？

早期把取帧、音视频同步和显示都放在渲染器中，导致 Renderer 同时管理时钟、队列和 OpenGL，职责过重，难以测试，也不方便插入图像处理。后来抽出 `VideoFrameScheduler`，再通过 `IVideoFrameSink` 将 Scheduler 与图像处理、视觉分析和 Renderer 连接起来，使调度和输出解耦。

另一个演进是队列从裸 `AVPacket*`、`AVFrame*` 加手工释放，改成 `unique_ptr + 自定义 deleter`，让所有权通过类型表达，而不是依赖调用方记住释放约定。

### 92. 如果重新做一次，你会在哪三个地方改变方案？

第一，更早定义 packet/frame 所有权以及 Stop、Interrupt、EOS 状态机，避免后期跨整条链路重构。第二，从项目初期建立单元测试、固定素材和性能基线，而不是扩展到多路后才系统测量。第三，视觉分析第一版就采用异步推理、降帧和结果缓存，避免先接入同步实现后再返工。

### 93. 项目距离真正工业视频平台还缺什么？

还缺自动重连和网络抖动恢复、异步或 GPU 推理、完整硬解零拷贝、录像、事件存储和历史查询、设备与权限管理、长期压力测试、崩溃恢复、日志监控、CI、标准 Release 安装包，以及真实业务中的报警规则、设备协议和操作闭环。

此外还需要明确支持矩阵，例如不同摄像头、Codec、分辨率、网络条件和 GPU 的兼容性，不能只以开发机运行成功作为工业交付标准。

### 94. 你从这个项目中获得的、能够迁移到其他 C++ 项目的能力是什么？

第一，用 RAII 和明确所有权处理 C++ 资源生命周期；第二，用状态机区分并发组件的暂停、停止、中断和自然结束；第三，通过接口和职责边界控制模块依赖，让功能能够渐进扩展；第四，性能问题先建立指标和可复现条件，再定位瓶颈，而不是凭感觉优化。

这些能力不仅适用于音视频，也适用于工业软件、设备上位机、网络客户端和其他多线程 C++ 系统。对我来说，项目最大的收获不是会调用多少 FFmpeg API，而是开始形成处理复杂状态、跨线程资源和架构演进的工程方法。

## 面试表述边界

以下内容当前不能作为已完成功能表述：

- 自动断线重连已经完成。
- 16 路在所有机器和所有视频规格下稳定满帧。
- YOLO 已经异步且具备工业实时性能。
- 当前是全链路 GPU 零拷贝。
- README 中的 CPU、内存和延迟目标已经完成标准化实测。
- 当前自动化测试已经覆盖真实音视频、摄像头、RTSP 和 OpenGL 全链路。
