#include "MediaPipeline.h"

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
    Q_UNUSED(mediaSource);
    Q_UNUSED(rendererContainer);
    return false;
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
    Q_UNUSED(mediaSource);
    Q_UNUSED(rendererContainer);
    return false;
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
