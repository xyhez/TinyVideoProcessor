//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"

#include <qlayout.h>

MediaPlayController::MediaPlayController(QObject *parent) {

}

MediaPlayController::~MediaPlayController() {

}

bool MediaPlayController::Initialize(const QString& filePath, QWidget *rendererContainer) {
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

    //6.初始化播放源
    mediaSource_ = std::make_shared<FileMediaSource>();
    mediaSource_->SetInfo(filePath);

    //7.链接信号槽
    CreateConnects();

    return true;
}

void MediaPlayController::CreateConnects() {
    connect(demuxerTH_,  &QThread::started,
        demuxer_, &demuxer::startDemuxer);

    connect(AdecoderTH_, &QThread::started,
        Adecoder_, &decoder::startDecoder);

    connect(VdecoderTH_, &QThread::started,
        Vdecoder_, &decoder::startDecoder);

    connect(audioplayTH_, &QThread::started,
        audioplay_, &AudioPlayer::startAudioPlay);
}

void MediaPlayController::ReleaseResources() {

}

void MediaPlayController::Start() {
    //1.打开媒体源
    mediaSource_->Open();

    //2.开启子线程
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


