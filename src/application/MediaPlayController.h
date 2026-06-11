#pragma  once
#ifndef TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
#define TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H

#include <QObject>
#include <QThread>
#include <memory>

#include "core/queue.h"
#include "domain/IMediaSource.h"
#include "infrastructure/audioplayer.h"
#include "infrastructure/decoder.h"
#include "infrastructure/demuxer.h"
#include "infrastructure/syncmanager.h"
#include "infrastructure/videorender.h"

constexpr size_t kPacketQueueMax = 30;
constexpr size_t kFrameQueueMax  = 30;

inline auto packetDeleter = [](AVPacket* p) { if (p) av_packet_free(&p); };
inline auto frameDeleter  = [](AVFrame*  f) { if (f) av_frame_free(&f);  };
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
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused
    };

    explicit MediaPlayController(QObject* parent = nullptr);
    ~MediaPlayController();

    // 初始化：传入媒体源+渲染容器
    bool Initialize(const QString& filePath,
        QWidget* rendererContainer);

    // 链接信号槽
    void CreateConnects();

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

    PlaybackState State() const;

signals:
    void StateChanged(PlaybackState state);

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
    std::unique_ptr<demuxer> demuxer_;
    std::unique_ptr<decoder> Adecoder_;
    std::unique_ptr<decoder> Vdecoder_;
    std::unique_ptr<AudioPlayer> audioplay_;
    VideoRender* videoRender_ = nullptr;

    // 线程
    std::unique_ptr<QThread> demuxerTH_;
    std::unique_ptr<QThread> AdecoderTH_;
    std::unique_ptr<QThread> VdecoderTH_;
    std::unique_ptr<QThread> audioplayTH_;

    PlaybackState state_ = PlaybackState::Stopped;

    void ReleaseResources();
    void SetState(PlaybackState state);
};


#endif //TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
