================================================================
  Phase 0 · Step 2  — 引入 IMediaSource 抽象 + 修复 syncmanager
================================================================

目标
----
  把 demuxer 与"文件路径"解耦，引入媒体源抽象接口，为后续摄
  像头/网络流扩展打地基。顺手修一下 syncmanager 的线程安全 bug。


--------------------------------------------------------------------
任务 1   新建 src/domain/IMediaSource.h
--------------------------------------------------------------------

  定义抽象接口，至少包含以下方法：

    enum class MediaSourceType { File, Camera, Network };

    class IMediaSource {
    public:
        virtual ~IMediaSource() = default;
        virtual bool open() = 0;
        virtual void close() = 0;
        virtual AVFormatContext* getFormatContext() = 0;
        virtual MediaSourceType  getType() const = 0;
        virtual QString          getSourceInfo() const = 0;
    };


--------------------------------------------------------------------
任务 2   新建 src/domain/FileMediaSource.h / .cpp
--------------------------------------------------------------------

  实现 IMediaSource，把 demuxer 现有的 avformat_open_input
  + 流信息发现逻辑搬过来：

    - 构造时接收 QString filePath
    - open()  内调用 avformat_open_input、avformat_find_stream_info
    - close() 内调用 avformat_close_input
    - 持有 AVFormatContext* m_ic
    - getFormatContext() 返回 m_ic
    - getType() 返回 MediaSourceType::File


--------------------------------------------------------------------
任务 3   改造 demuxer
--------------------------------------------------------------------

  让 demuxer 不再自己 open 文件，改为持有一个 IMediaSource：

    [+]  构造函数加参数  std::shared_ptr<IMediaSource>
    [-]  删除  initDemuxer(QString filename)  方法
    [-]  删除  m_videoFileName 成员
    [-]  删除  avformat_open_input 等所有打开/关闭文件的代码
    [~]  startDemuxer() 里 ic 改为 m_source->getFormatContext()
    [~]  析构里不要再 close，让 IMediaSource 自己管


--------------------------------------------------------------------
任务 4   改造 MainWindow
--------------------------------------------------------------------

  之前：
    m_demuxer = new demuxer(...);
    m_demuxer->initDemuxer(m_videoFileName);

  之后：
    auto source = std::make_shared<FileMediaSource>(m_videoFileName);
    source->open();
    m_demuxer = new demuxer(source,
                            m_APacket, m_VPacket,
                            m_AFrame, m_VFrame);


--------------------------------------------------------------------
任务 5   修复 syncmanager::getClock() 线程安全
--------------------------------------------------------------------

  文件:  src/infrastructure/syncmanager.h / .cpp

    [~]  成员  QMutex mutex;     →  mutable QMutex mutex;
    [~]  getClock() const 内部加锁：

         double syncmanager::getClock() const {
             QMutexLocker locker(&mutex);
             return mainclock;
         }


--------------------------------------------------------------------
任务 6   更新 CMakeLists.txt
--------------------------------------------------------------------

  PROJECT_SOURCES 里追加：

    src/domain/IMediaSource.h
    src/domain/FileMediaSource.h
    src/domain/FileMediaSource.cpp


====================================================================
验收标准
====================================================================

  [ ]  编译零警告
  [ ]  播放功能与 Step 1b 完全一致
  [ ]  demuxer 里不再出现 avformat_open_input
  [ ]  Camera / Network 留个枚举值即可，本步不用实现

  任务 1-4 是核心，5、6 顺手做。改完跑一下播放测试。
