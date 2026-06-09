//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"

#include <qlayout.h>

MediaPlayController::MediaPlayController(QObject *parent) {

}

MediaPlayController::~MediaPlayController() {

}

bool MediaPlayController::Initialize(std::shared_ptr<IMediaSource> source, QWidget *rendererContainer) {
    // 1.初始化队列
    APacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    VPacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    AFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);
    VFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);

    //2.基层对象构造
    demuxer_ = new demuxer(mediaSource_,APacket_,VPacket_,AFrame_,VFrame_);
    Adecoder_ = new decoder();
    Vdecoder_ = new decoder();
    audioplay_ = new AudioPlayer();
    videoRender_ = new VideoRender();
    syncManager_ = std::make_shared<syncmanager>();

    // 3.子线程初始化
    demuxerTH_ = new QThread(this);
    AdecoderTH_ = new QThread(this);
    VdecoderTH_ = new QThread(this);
    audioplayTH_ = new QThread(this);

    // 4.将对象初始化并移动到对应的线程
    Adecoder_->initDecoder(APacket_,AFrame_,demuxer_->reAcoderPar(),demuxer_->reAtimebase());
    Adecoder_->moveToThread(AdecoderTH_);
    Vdecoder_->initDecoder(VPacket_,VFrame_,demuxer_->reVcoderPar(),demuxer_->reVtimebase());
    Vdecoder_->moveToThread(VdecoderTH_);
    audioplay_->initAudioPlay(demuxer_->reAcoderPar(),AFrame_,syncManager_);
    audioplay_->moveToThread(audioplayTH_);
    videoRender_->initvideoRender(VFrame_,syncManager_);

    //5.设置videorender
    if (rendererContainer) {
        rendererContainer->setLayout(new QVBoxLayout());
        rendererContainer->layout()->addWidget(videoRender_);
    }

    return true;
}

void MediaPlayController::CreateConnections() {

}

void MediaPlayController::ReleaseResources() {

}

void MediaPlayController::Start() {

}

void MediaPlayController::Stop() {

}

void MediaPlayController::Pause() {

}

void MediaPlayController::VolumeUp() {

}

void MediaPlayController::VolumeDown() {

}

void MediaPlayController::Rewind() {

}

void MediaPlayController::Forward() {

}


