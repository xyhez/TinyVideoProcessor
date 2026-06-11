//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"
#include "domain/FileMediaSource.h"

#include <qlayout.h>

MediaPlayController::MediaPlayController(QObject *parent)
    : QObject(parent)
{

}

MediaPlayController::~MediaPlayController() {
    ReleaseResources();
}

bool MediaPlayController::Initialize(const QString& filePath, QWidget *rendererContainer) {
    // 1.初始化队列
    APacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    VPacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    AFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);
    VFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);

    //2.初始化媒体源
    mediaSource_ = std::make_shared<FileMediaSource>();
    mediaSource_->SetInfo(filePath);
    if (!mediaSource_->Open()) {
        qCritical() << "媒体源打开失败:" << filePath;
        return false;
    }

    //3.基层对象构造
    demuxer_ = std::make_unique<demuxer>(mediaSource_,APacket_,VPacket_,AFrame_,VFrame_);
    const Fileinfo& sourceInfo = mediaSource_->GetSourceInfo();
    Adecoder_ = std::make_unique<decoder>();
    Vdecoder_ = std::make_unique<decoder>();
    audioplay_ = std::make_unique<AudioPlayer>();
    videoRender_ = new VideoRender(rendererContainer);
    syncManager_ = std::make_shared<syncmanager>();

    // 4.子线程初始化
    demuxerTH_ = std::make_unique<QThread>();
    AdecoderTH_ = std::make_unique<QThread>();
    VdecoderTH_ = std::make_unique<QThread>();
    audioplayTH_ = std::make_unique<QThread>();

    // 5.将对象初始化并移动到对应的线程
    demuxer_->moveToThread(demuxerTH_.get());
    Adecoder_->initDecoder(APacket_,AFrame_,demuxer_->reAcoderPar(),demuxer_->reAtimebase());
    Adecoder_->moveToThread(AdecoderTH_.get());
    Vdecoder_->initDecoder(VPacket_,VFrame_,demuxer_->reVcoderPar(),demuxer_->reVtimebase());
    Vdecoder_->moveToThread(VdecoderTH_.get());
    audioplay_->initAudioPlay(demuxer_->reAcoderPar(), AFrame_, syncManager_, sourceInfo.A_timeBase);
    audioplay_->moveToThread(audioplayTH_.get());
    videoRender_->initvideoRender(VFrame_, syncManager_, sourceInfo.FrameRate, sourceInfo.V_timeBase);

    //6.设置videorender
    if (rendererContainer) {
        rendererContainer->setLayout(new QVBoxLayout());
        rendererContainer->layout()->addWidget(videoRender_);
    }

    //7.链接信号槽
    CreateConnects();

    return true;
}

void MediaPlayController::CreateConnects() {
    connect(demuxerTH_.get(),  &QThread::started,
        demuxer_.get(), &demuxer::startDemuxer);

    connect(AdecoderTH_.get(), &QThread::started,
        Adecoder_.get(), &decoder::startDecoder);

    connect(VdecoderTH_.get(), &QThread::started,
        Vdecoder_.get(), &decoder::startDecoder);

    connect(audioplayTH_.get(), &QThread::started,
        audioplay_.get(), &AudioPlayer::startAudioPlay);
}

void MediaPlayController::ReleaseResources() {
    // 确保所有工作循环已停止（防 Stop() 未调）
    if (APacket_) { APacket_->SetStopFlag(true); APacket_->WakeAll(); }
    if (VPacket_) { VPacket_->SetStopFlag(true); VPacket_->WakeAll(); }
    if (AFrame_)  { AFrame_->SetStopFlag(true);  AFrame_->WakeAll();  }
    if (VFrame_)  { VFrame_->SetStopFlag(true);  VFrame_->WakeAll();  }

    if (demuxer_)   demuxer_->stop();
    if (Adecoder_)  Adecoder_->stop();
    if (Vdecoder_)  Vdecoder_->stop();
    if (audioplay_) audioplay_->stop();
    if (videoRender_) videoRender_->stopRendering();

    auto stopThread = [](std::unique_ptr<QThread>& th) {
        if (th) {
            th->quit();
            th->wait();
            th.reset();
        }
    };
    stopThread(demuxerTH_);
    stopThread(AdecoderTH_);
    stopThread(VdecoderTH_);
    stopThread(audioplayTH_);

    demuxer_.reset();
    Adecoder_.reset();
    Vdecoder_.reset();
    audioplay_.reset();
    // videoRender_ 由 Qt 父子树管理，无需 delete

    APacket_.reset();
    VPacket_.reset();
    AFrame_.reset();
    VFrame_.reset();
    syncManager_.reset();
    mediaSource_.reset();
}

MediaPlayController::PlaybackState MediaPlayController::state() const {
    return state_;
}

void MediaPlayController::SetState(PlaybackState state) {
    if (state_ == state) {
        return;
    }

    state_ = state;
    emit stateChanged(state_);
}

void MediaPlayController::Start() {
    SetState(PlaybackState::Playing);

    demuxerTH_->start();
    AdecoderTH_->start();
    VdecoderTH_->start();
    audioplayTH_->start();

    demuxer_->start();
    Adecoder_->start();
    Vdecoder_->start();
    audioplay_->start();
    videoRender_->startRendering();
}

void MediaPlayController::Stop() {
    SetState(PlaybackState::Stopped);

    APacket_->SetStopFlag(true);
    VPacket_->SetStopFlag(true);
    AFrame_->SetStopFlag(true);
    VFrame_->SetStopFlag(true);

    APacket_->WakeAll();
    VPacket_->WakeAll();
    AFrame_->WakeAll();
    VFrame_->WakeAll();

    demuxer_->stop();
    Adecoder_->stop();
    Vdecoder_->stop();
    audioplay_->stop();
    videoRender_->stopRendering();
}

void MediaPlayController::Pause() {
    SetState(PlaybackState::Paused);
    demuxer_->pause();
    Adecoder_->pause();
    Vdecoder_->pause();
    audioplay_->pause();
    videoRender_->pause();
}

void MediaPlayController::VolumeUp() {
    audioplay_->volumeUp();
}

void MediaPlayController::VolumeDown() {
    audioplay_->volumeDown();
}

void MediaPlayController::Rewind() {
    Pause();
    demuxer_->Rewind();
    Start();
}

void MediaPlayController::Forward() {
    Pause();
    syncManager_->Forword();
    demuxer_->Forward();
    Start();
}


