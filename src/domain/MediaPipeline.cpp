#include "MediaPipeline.h"

#include <QDebug>
#include <QVBoxLayout>

namespace {
constexpr size_t kPacketQueueMax = 30;
constexpr size_t kFrameQueueMax = 30;

auto packetDeleter = [](AVPacket* packet) {
    if (packet) {
        av_packet_free(&packet);
    }
};

auto frameDeleter = [](AVFrame* frame) {
    if (frame) {
        av_frame_free(&frame);
    }
};
}

MediaPipeline::MediaPipeline(QObject* parent)
    : QObject(parent)
{
}

MediaPipeline::~MediaPipeline()
{
    Shutdown();
}

bool MediaPipeline::Initialize(std::shared_ptr<IMediaSource> mediaSource,
                               QWidget* rendererContainer)
{
    if (!mediaSource) {
        qCritical() << "MediaPipeline initialize failed: media source is null";
        return false;
    }

    if (!CreatePipelineResources(mediaSource, rendererContainer)) {
        ReleaseResources();
        return false;
    }

    return true;
}

void MediaPipeline::Shutdown()
{
}

void MediaPipeline::Start()
{
}

void MediaPipeline::Pause()
{
}

void MediaPipeline::Stop()
{
}

bool MediaPipeline::SeekTo(double seconds)
{
    Q_UNUSED(seconds);
    return false;
}

bool MediaPipeline::SeekRelative(double seconds)
{
    Q_UNUSED(seconds);
    return false;
}

double MediaPipeline::Duration() const
{
    return 0.0;
}

double MediaPipeline::Position() const
{
    return 0.0;
}

void MediaPipeline::SetPlaybackSpeed(double speed)
{
    Q_UNUSED(speed);
}

void MediaPipeline::SetVolume(double volume)
{
    Q_UNUSED(volume);
}

void MediaPipeline::SetMuted(bool muted)
{
    Q_UNUSED(muted);
}

bool MediaPipeline::CreatePipelineResources(std::shared_ptr<IMediaSource> mediaSource,
                                            QWidget* rendererContainer)
{
    mediaSource_ = mediaSource;

    VPacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    VFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax, frameDeleter);

    demuxer_ = std::make_unique<demuxer>(mediaSource_, APacket_, VPacket_, AFrame_, VFrame_);

    const Fileinfo& sourceInfo = mediaSource_->GetSourceInfo();
    const bool hasAudio = sourceInfo.hasAudio_;
    if (hasAudio) {
        APacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
        AFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax, frameDeleter);
        demuxer_->SetAudioQueues(APacket_, AFrame_);
    }

    Vdecoder_ = std::make_unique<decoder>();
    if (hasAudio) {
        Adecoder_ = std::make_unique<decoder>();
        audioplay_ = std::make_unique<AudioPlayer>();
    }

    videoRender_ = new VideoRender(rendererContainer);
    syncManager_ = std::make_shared<syncmanager>();
    syncManager_->SetSyncType(hasAudio ? SYNCTYPE::AUDIO : SYNCTYPE::VIDEO);
    positionTimer_ = std::make_unique<QTimer>();
    positionTimer_->setInterval(200);

    demuxerTH_ = std::make_unique<QThread>();
    VdecoderTH_ = std::make_unique<QThread>();
    if (hasAudio) {
        AdecoderTH_ = std::make_unique<QThread>();
        audioplayTH_ = std::make_unique<QThread>();
    }

    demuxer_->moveToThread(demuxerTH_.get());
    if (hasAudio) {
        Adecoder_->InitDecoder(APacket_, AFrame_, demuxer_->ReAcoderPar(), demuxer_->ReAtimebase());
        Adecoder_->moveToThread(AdecoderTH_.get());
    }
    Vdecoder_->InitDecoder(VPacket_, VFrame_, demuxer_->ReVcoderPar(), demuxer_->ReVtimebase());
    Vdecoder_->moveToThread(VdecoderTH_.get());

    if (hasAudio) {
        audioplay_->InitAudioPlay(demuxer_->ReAcoderPar(), AFrame_, syncManager_, sourceInfo.A_timeBase);
        audioplay_->moveToThread(audioplayTH_.get());
    }
    videoRender_->InitVideoRender(VFrame_, syncManager_, sourceInfo.FrameRate, sourceInfo.V_timeBase);

    if (rendererContainer) {
        if (!rendererContainer->layout()) {
            rendererContainer->setLayout(new QVBoxLayout());
        }
        rendererContainer->layout()->addWidget(videoRender_);
    }

    return true;
}

void MediaPipeline::CreateConnects()
{
}

void MediaPipeline::ReleaseResources()
{
}

void MediaPipeline::HandleDemuxFinished()
{
}

void MediaPipeline::ClearQueues()
{
}

void MediaPipeline::FlushDecoders()
{
}
