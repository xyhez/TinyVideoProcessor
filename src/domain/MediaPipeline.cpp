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

    CreateConnects();

    return true;
}

void MediaPipeline::Shutdown()
{
    ReleaseResources();
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
    return mediaSource_ ? static_cast<double>(mediaSource_->GetSourceInfo().duration_) : 0.0;
}

double MediaPipeline::Position() const
{
    return syncManager_ ? syncManager_->GetClock() / 1000.0 : 0.0;
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
    connect(demuxerTH_.get(), &QThread::started,
            demuxer_.get(), &demuxer::StartDemuxer);

    if (AdecoderTH_ && Adecoder_) {
        connect(AdecoderTH_.get(), &QThread::started,
                Adecoder_.get(), &decoder::StartDecoder);
    }

    connect(VdecoderTH_.get(), &QThread::started,
            Vdecoder_.get(), &decoder::StartDecoder);

    if (audioplayTH_ && audioplay_) {
        connect(audioplayTH_.get(), &QThread::started,
                audioplay_.get(), &AudioPlayer::StartAudioPlay);
    }

    connect(demuxer_.get(), &demuxer::DemuxFinished,
            this, &MediaPipeline::HandleDemuxFinished);

    connect(positionTimer_.get(), &QTimer::timeout, this, [this]() {
        emit PositionChanged(Position());
    });

    if (audioplay_) {
        connect(audioplay_.get(), &AudioPlayer::VolumeChanged, this, [this](qreal volume) {
            emit VolumeChanged(static_cast<double>(volume));
        });

        connect(audioplay_.get(), &AudioPlayer::MuteChanged, this, [this](bool muted) {
            emit MuteChanged(muted);
        });
    }
}

void MediaPipeline::ReleaseResources()
{
    if (positionTimer_) {
        positionTimer_->stop();
    }

    if (APacket_) { APacket_->RequestStop(); }
    if (VPacket_) { VPacket_->RequestStop(); }
    if (AFrame_)  { AFrame_->RequestStop();  }
    if (VFrame_)  { VFrame_->RequestStop();  }

    if (demuxer_) {
        demuxer_->Stop();
    }
    if (Adecoder_) {
        Adecoder_->Stop();
    }
    if (Vdecoder_) {
        Vdecoder_->Stop();
    }
    if (audioplay_) {
        audioplay_->Stop();
    }
    if (videoRender_) {
        videoRender_->StopRendering();
    }

    auto stopThread = [](std::unique_ptr<QThread>& thread) {
        if (thread) {
            thread->quit();
            thread->wait();
            thread.reset();
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
    if (videoRender_) {
        delete videoRender_;
        videoRender_ = nullptr;
    }

    APacket_.reset();
    VPacket_.reset();
    AFrame_.reset();
    VFrame_.reset();
    syncManager_.reset();
    if (mediaSource_) {
        mediaSource_->Close();
    }
    mediaSource_.reset();
    positionTimer_.reset();
}

void MediaPipeline::HandleDemuxFinished()
{
    if (APacket_) {
        APacket_->RequestStop();
    }
    if (VPacket_) {
        VPacket_->RequestStop();
    }
    if (positionTimer_) {
        positionTimer_->stop();
    }

    emit PositionChanged(Duration());
    emit PlaybackFinished();
}

void MediaPipeline::ClearQueues()
{
    if (APacket_) { APacket_->ClearAndWake(); }
    if (VPacket_) { VPacket_->ClearAndWake(); }
    if (AFrame_)  { AFrame_->ClearAndWake();  }
    if (VFrame_)  { VFrame_->ClearAndWake();  }
}

void MediaPipeline::FlushDecoders()
{
    if (Adecoder_) {
        Adecoder_->ClearDiscardBefore();
        Adecoder_->Flush();
    }
    if (Vdecoder_) {
        Vdecoder_->ClearDiscardBefore();
        Vdecoder_->Flush();
    }
}
