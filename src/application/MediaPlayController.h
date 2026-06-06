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

    void CreateConnections();
    void ReleaseResources();
};


#endif //TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
