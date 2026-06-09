#pragma  once
#ifndef TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
#define TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H

#include <QObject>

#include "core/queue.h"
#include "domain/IMediaSource.h"
#include "infrastructure/audioplayer.h"
#include "infrastructure/decoder.h"
#include "infrastructure/demuxer.h"
#include "infrastructure/syncmanager.h"
#include "infrastructure/videorender.h"

constexpr size_t kPacketQueueMax = 30;
constexpr size_t kFrameQueueMax  = 30;

auto packetDeleter = [](AVPacket* p) { if (p) av_packet_free(&p); };
auto frameDeleter  = [](AVFrame*  f) { if (f) av_frame_free(&f);  };
/**
 * @brief
 * MediaPlayController是应用层的协调者，
 * 是MainWindow(UI层)和基础设施层组件(demuxer/decoder/player/render)之间的中间层。
 * MainWindow(UI) -按钮事件 -键盘事件 -显示窗口
 * MediaPlayerController(应用层) -协调所有播放组件 -管理生命周期 -处理业务逻辑
 * 基础设施层 -demuxer/decoder/audioplayer/videorender/syncmanager
 */
class MediaPlayController :public QObject{
    Q_OBJECT
public:
    explicit MediaPlayController(QObject* parent = nullptr);
    ~MediaPlayController();

    // 初始化：传入媒体源+渲染容器
    bool Initialize(std::shared_ptr<IMediaSource> source,
        QWidget* rendererContainer);

    // 播放控制
    void Start();
    void Stop();
    void Pause();

    // 音量控制
    void VolumeUp();
    void VolumeDown();

    // seek
    void Rewind();
    void Forward();

private:
    // 队列
    std::shared_ptr<SyncQueue<AVPacket*>> APacket_ = nullptr;
    std::shared_ptr<SyncQueue<AVPacket*>> VPacket_ = nullptr;
    std::shared_ptr<SyncQueue<AVFrame*>>  AFrame_ = nullptr;
    std::shared_ptr<SyncQueue<AVFrame*>>  VFrame_ = nullptr;

    // 媒体源
    std::shared_ptr<IMediaSource> mediaSource_;

    // 同步
    std::shared_ptr<syncmanager> syncManager_;

    // 组件
    demuxer*     demuxer_     = nullptr;
    decoder*     Adecoder_   = nullptr;
    decoder*     Vdecoder_   = nullptr;
    AudioPlayer* audioplay_   = nullptr;
    VideoRender* videoRender_ = nullptr;

    // 线程
    QThread* demuxerTH_    = nullptr;
    QThread* AdecoderTH_  = nullptr;
    QThread* VdecoderTH_  = nullptr;
    QThread* audioplayTH_  = nullptr;

    bool m_pauseFlag = false;

    void CreateConnections();
    void ReleaseResources();
};


#endif //TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
