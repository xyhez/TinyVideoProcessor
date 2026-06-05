目标
----
  从 MainWindow 中分离播放控制逻辑，建立应用层（Application
  Layer）。MainWindow 只负责 UI 交互，Controller 负责协调播放
  管线（demuxer/decoder/player/render/syncmanager）的生命周期。

  最终验收：MainWindow.cpp 行数 < 150。


设计决策（已确认）
------------------
  1. VideoRender 嵌入方式：
     Controller 接收 MainWindow 提供的容器 QWidget*，
     由 Controller 内部创建并嵌入 VideoRender。

  2. syncmanager 归属：
     由 Controller 创建并管理（属于播放管线）。

  3. MediaSource 归属：
     由 MainWindow 创建（涉及 UI 交互如选文件），
     通过 initialize() 传给 Controller。


--------------------------------------------------------------------
任务 1   新建 src/application/MediaPlayerController.h / .cpp
--------------------------------------------------------------------

  定义 Controller 类，接管 MainWindow 的播放管线：

    class MediaPlayerController : public QObject {
        Q_OBJECT
    public:
        explicit MediaPlayerController(QObject *parent = nullptr);
        ~MediaPlayerController();

        // 初始化：传入媒体源 + 渲染容器
        bool initialize(std::shared_ptr<IMediaSource> source,
                        QWidget* renderContainer);

        // 播放控制
        void start();
        void pause();
        void stop();

        // 音量控制
        void volumeUp();
        void volumeDown();

        // Seek
        void rewind();
        void forward();

    signals:
        void errorOccurred(QString msg);

    private:
        // 队列
        std::shared_ptr<SyncQueue<AVPacket*>> m_APacket;
        std::shared_ptr<SyncQueue<AVPacket*>> m_VPacket;
        std::shared_ptr<SyncQueue<AVFrame*>>  m_AFrame;
        std::shared_ptr<SyncQueue<AVFrame*>>  m_VFrame;

        // 媒体源
        std::shared_ptr<IMediaSource> m_mediaSource;

        // 同步
        std::shared_ptr<syncmanager> m_syncManager;

        // 组件
        demuxer*     m_demuxer     = nullptr;
        decoder*     m_A_decoder   = nullptr;
        decoder*     m_V_decoder   = nullptr;
        AudioPlayer* m_audioplay   = nullptr;
        VideoRender* m_videoRender = nullptr;

        // 线程
        QThread* m_demuxerTH    = nullptr;
        QThread* m_A_decoderTH  = nullptr;
        QThread* m_V_decoderTH  = nullptr;
        QThread* m_audioplayTH  = nullptr;

        bool m_pauseFlag = false;

        void createConnections();
        void releaseResources();
    };


--------------------------------------------------------------------
任务 2   迁移成员变量
--------------------------------------------------------------------

  从 MainWindow 移除以下成员，全部移到 Controller：

    [-]  m_APacket / m_VPacket / m_AFrame / m_VFrame
    [-]  m_demuxer / m_A_decoder / m_V_decoder
    [-]  m_demuxerTH / m_A_decoderTH / m_V_decoderTH
    [-]  m_syncManager
    [-]  m_audioplay / m_audioplayTH
    [-]  m_videoRender
    [-]  pause_flag
    [-]  mutex (未使用)

  MainWindow 新增：
    [+]  MediaPlayerController* m_controller;
    [+]  QString m_videoFileName (保留用户选择的路径)


--------------------------------------------------------------------
任务 3   迁移初始化逻辑
--------------------------------------------------------------------

  当前 MainWindow 构造函数中的初始化代码：
    - 创建队列 + 创建组件 + 创建线程 + initDecoder + initAudioPlay
    - 创建 VideoRender 并嵌入 ui->openGLWidget
    - createConnect()

  全部迁移到 MediaPlayerController::initialize()，
  MainWindow 构造函数只剩：

    MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent), ui(new Ui::MainWindow)
        , m_controller(new MediaPlayerController(this))
    {
        ui->setupUi(this);
        this->setWindowTitle("modernFFplay-V1.0");

        auto source = std::make_shared<FileMediaSource>();
        source->SetInfo(m_videoFileName);
        if (!source->open()) {
            qCritical() << "媒体源打开失败";
            return;
        }
        m_controller->initialize(source, ui->openGLWidget);
    }


--------------------------------------------------------------------
任务 4   迁移播放控制方法
--------------------------------------------------------------------

  从 MainWindow 迁出以下方法，改为转发：

    [~]  start()       →  m_controller->start();
    [~]  pause()       →  m_controller->pause();
    [~]  stop()        →  m_controller->stop();

  Controller::start() 启动所有线程：
    - demuxer / A_decoder / V_decoder / audioplay 的 start()
      和 thread->start()
    - videoRender->startRendering()

  Controller::pause() 暂停所有组件。
  Controller::stop() 停止所有组件 + 队列 SetStopFlag/WakeAll。


--------------------------------------------------------------------
任务 5   迁移键盘事件转发
--------------------------------------------------------------------

  MainWindow::keyReleaseEvent 保留外壳，内部调用 Controller：

    [~]  Space  →  pause/start (通过 Controller)
    [~]  Up     →  m_controller->volumeUp();
    [~]  Down   →  m_controller->volumeDown();
    [~]  Left   →  m_controller->rewind();
    [~]  Right  →  m_controller->forward();

  Controller::forward() 内部完成原有的：
    pause → syncManager->Forword() → emit e_Forward → start


--------------------------------------------------------------------
任务 6   迁移资源清理
--------------------------------------------------------------------

  MainWindow::closeEvent 简化为：

    void MainWindow::closeEvent(QCloseEvent *event) {
        Q_UNUSED(event);
        m_controller->stop();
    }

  原来的 4 个队列 SetStopFlag/WakeAll 移到 Controller::stop()。
  原来的 freeRsources() 改名为 Controller::releaseResources()，
  在 Controller 析构时调用。


--------------------------------------------------------------------
任务 7   信号槽迁移
--------------------------------------------------------------------

  当前 MainWindow::createConnect() 中的连接：

    connect(m_demuxerTH,    &QThread::started, m_demuxer,    &demuxer::startDemuxer);
    connect(m_A_decoderTH,  &QThread::started, m_A_decoder,  &decoder::startDecoder);
    connect(m_V_decoderTH,  &QThread::started, m_V_decoder,  &decoder::startDecoder);
    connect(m_audioplayTH,  &QThread::started, m_audioplay,  &AudioPlayer::startAudioPlay);

  全部移到 Controller::createConnections()。

  MainWindow 中原有的 signals (e_Rewind/e_Forward 等) 暂时保留，
  如果 Controller 用不到，再清理。


--------------------------------------------------------------------
任务 8   更新 CMakeLists.txt
--------------------------------------------------------------------

  PROJECT_SOURCES 里追加：

    src/application/MediaPlayerController.h
    src/application/MediaPlayerController.cpp


====================================================================
验收标准
====================================================================

  [ ]  编译零警告
  [ ]  播放功能与 Step 2 完全一致
  [ ]  MainWindow.cpp 行数 < 150
  [ ]  MainWindow 不再直接持有 demuxer/decoder/audioplay 等组件
  [ ]  MainWindow 不再 #include 基础设施层头文件
       （只 include MediaPlayerController + IMediaSource/FileMediaSource）
  [ ]  closeEvent 一行：m_controller->stop();

  顺序：先建 Controller 骨架（任务 1），再逐个搬运
  （任务 2-7）。每搬一块编译一次，确保不破坏播放。
