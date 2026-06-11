//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"
#include <qlayout.h>

MediaPlayController::MediaPlayController(QObject *parent)
    : QObject(parent)
{

}

MediaPlayController::~MediaPlayController() {
    ReleaseResources();
}

bool MediaPlayController::Initialize(std::shared_ptr<IMediaSource> mediaSource, QWidget *rendererContainer) {
    // 1.初始化队列
    APacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    VPacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    AFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);
    VFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);

    //2.初始化媒体源
    if (!mediaSource) {
        qCritical() << "Media source is null";
        return false;
    }

    mediaSource_ = mediaSource;
    if (!mediaSource_->Open()) {
        qCritical() << "Failed to open media source";
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
    Adecoder_->InitDecoder(APacket_,AFrame_,demuxer_->ReAcoderPar(),demuxer_->ReAtimebase());
    Adecoder_->moveToThread(AdecoderTH_.get());
    Vdecoder_->InitDecoder(VPacket_,VFrame_,demuxer_->ReVcoderPar(),demuxer_->ReVtimebase());
    Vdecoder_->moveToThread(VdecoderTH_.get());
    audioplay_->InitAudioPlay(demuxer_->ReAcoderPar(), AFrame_, syncManager_, sourceInfo.A_timeBase);
    audioplay_->moveToThread(audioplayTH_.get());
    videoRender_->InitVideoRender(VFrame_, syncManager_, sourceInfo.FrameRate, sourceInfo.V_timeBase);

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
        demuxer_.get(), &demuxer::StartDemuxer);

    connect(AdecoderTH_.get(), &QThread::started,
        Adecoder_.get(), &decoder::StartDecoder);

    connect(VdecoderTH_.get(), &QThread::started,
        Vdecoder_.get(), &decoder::StartDecoder);

    connect(audioplayTH_.get(), &QThread::started,
        audioplay_.get(), &AudioPlayer::StartAudioPlay);
}

void MediaPlayController::ReleaseResources() {
    // 确保所有工作循环已停止（防 Stop() 未调）
    if (APacket_) { APacket_->SetStopFlag(true); APacket_->WakeAll(); }
    if (VPacket_) { VPacket_->SetStopFlag(true); VPacket_->WakeAll(); }
    if (AFrame_)  { AFrame_->SetStopFlag(true);  AFrame_->WakeAll();  }
    if (VFrame_)  { VFrame_->SetStopFlag(true);  VFrame_->WakeAll();  }

    if (demuxer_)   demuxer_->Stop();
    if (Adecoder_)  Adecoder_->Stop();
    if (Vdecoder_)  Vdecoder_->Stop();
    if (audioplay_) audioplay_->Stop();
    if (videoRender_) videoRender_->StopRendering();

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

MediaPlayController::PlaybackState MediaPlayController::State() const {
    return state_;
}

void MediaPlayController::SetState(PlaybackState state) {
    if (state_ == state) {
        return;
    }

    state_ = state;
    emit StateChanged(state_);
}

void MediaPlayController::Start() {
    SetState(PlaybackState::Playing);

    demuxerTH_->start();
    AdecoderTH_->start();
    VdecoderTH_->start();
    audioplayTH_->start();

    demuxer_->Start();
    Adecoder_->Start();
    Vdecoder_->Start();
    audioplay_->Start();
    videoRender_->StartRendering();
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

    demuxer_->Stop();
    Adecoder_->Stop();
    Vdecoder_->Stop();
    audioplay_->Stop();
    videoRender_->StopRendering();
}

void MediaPlayController::Pause() {
    SetState(PlaybackState::Paused);
    demuxer_->Pause();
    Adecoder_->Pause();
    Vdecoder_->Pause();
    audioplay_->Pause();
    videoRender_->Pause();
}

void MediaPlayController::VolumeUp() {
    audioplay_->VolumeUp();
}

void MediaPlayController::VolumeDown() {
    audioplay_->VolumeDown();
}

void MediaPlayController::Rewind() {
    Pause();
    demuxer_->Rewind();
    Start();
}

void MediaPlayController::Forward() {
    Pause();
    syncManager_->Forward();
    demuxer_->Forward();
    Start();
}


