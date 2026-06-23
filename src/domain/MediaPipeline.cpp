#include "MediaPipeline.h"
#include "core/AppLog.h"

#include <algorithm>
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
        APP_LOG_CRITICAL("MediaPipeline initialize failed: media source is null");
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
    if (!demuxer_ || !Vdecoder_ || !videoRender_ || !positionTimer_ || !demuxerTH_ || !VdecoderTH_) {
        APP_LOG_WARN("MediaPipeline start failed: playback pipeline is not initialized");
        emit ErrorOccurred("Playback pipeline is not initialized");
        return;
    }

    const bool hasAudio = Adecoder_ || audioplay_ || AdecoderTH_ || audioplayTH_;
    if (hasAudio && (!Adecoder_ || !audioplay_ || !AdecoderTH_ || !audioplayTH_)) {
        APP_LOG_WARN("MediaPipeline start failed: audio pipeline is incomplete");
        emit ErrorOccurred("Audio pipeline is incomplete");
        return;
    }

    positionTimer_->start();
    emit PositionChanged(Position());
    emit DurationChanged(Duration());

    demuxerTH_->start();
    VdecoderTH_->start();
    if (hasAudio) {
        AdecoderTH_->start();
        audioplayTH_->start();
    }

    demuxer_->Start();
    Vdecoder_->Start();
    if (hasAudio) {
        Adecoder_->Start();
        audioplay_->Start();
    }
    videoRender_->StartRendering();
}

void MediaPipeline::Pause()
{
    if (!demuxer_ || !Vdecoder_ || !videoRender_ || !positionTimer_) {
        APP_LOG_WARN("MediaPipeline pause failed: playback pipeline is not initialized");
        emit ErrorOccurred("Playback pipeline is not initialized");
        return;
    }

    emit PositionChanged(Position());
    demuxer_->Pause();
    if (Adecoder_) {
        Adecoder_->Pause();
    }
    Vdecoder_->Pause();
    if (audioplay_) {
        audioplay_->Pause();
    }
    videoRender_->Pause();
    positionTimer_->stop();
}

void MediaPipeline::Stop()
{
    if (!demuxer_ || !Vdecoder_ || !syncManager_ || !positionTimer_) {
        return;
    }

    if (!SuspendPipelineAndWait()) {
        APP_LOG_WARN("MediaPipeline stop failed: playback pipeline suspend failed");
        emit ErrorOccurred("Playback pipeline suspend failed");
        return;
    }

    emit PositionChanged(0.0);
    positionTimer_->stop();

    if (mediaSource_ && !mediaSource_->IsSeekable()) {
        ResetRealtimePipelineStop();
        return;
    }

    ResetPipelineToStart();
}

bool MediaPipeline::SeekTo(double seconds)
{
    if (!demuxer_ || !Vdecoder_ || !syncManager_) {
        APP_LOG_WARN("MediaPipeline seek failed: playback pipeline is not initialized");
        emit ErrorOccurred("Playback pipeline is not initialized");
        return false;
    }

    // 检查媒体源是否支持 seek
    if (mediaSource_ && !mediaSource_->IsSeekable()) {
        APP_LOG_WARN("MediaPipeline seek rejected: media source is not seekable");
        return false;
    }

    double targetSeconds = seconds;
    if (targetSeconds < 0.0) {
        targetSeconds = 0.0;
    }

    const double durationSeconds = Duration();
    if (durationSeconds > 0.0 && targetSeconds > durationSeconds) {
        targetSeconds = durationSeconds;
    }

    if (!SuspendPipelineAndWait()) {
        APP_LOG_WARN("MediaPipeline seek failed: playback pipeline suspend failed");
        emit ErrorOccurred("Playback pipeline suspend failed");
        return false;
    }

    ClearQueues();

    const int seekResult = demuxer_->SeekTo(targetSeconds);
    if (seekResult != 0) {
        APP_LOG_WARN("MediaPipeline seek failed: demuxer rejected target", targetSeconds);
        emit ErrorOccurred("Demuxer rejected seek target");
        return false;
    }

    FlushDecoders();
    Vdecoder_->SetDiscardBefore(targetSeconds);
    ClearQueues();
    syncManager_->SeekTo(targetSeconds * 1000.0);

    emit PositionChanged(Position());

    return true;
}

bool MediaPipeline::SeekRelative(double seconds)
{
    if (!demuxer_) {
        APP_LOG_WARN("MediaPipeline seek failed: demuxer is not initialized");
        emit ErrorOccurred("Demuxer is not initialized");
        return false;
    }

    // 检查媒体源是否支持 seek
    if (mediaSource_ && !mediaSource_->IsSeekable()) {
        APP_LOG_WARN("MediaPipeline seek rejected: media source is not seekable");
        return false;
    }

    return SeekTo(demuxer_->Position() + seconds);
}

double MediaPipeline::Duration() const
{
    if (!mediaSource_ || !mediaSource_->HasDuration()) {
        return 0.0;
    }

    return static_cast<double>(mediaSource_->GetSourceInfo().duration_);
}

double MediaPipeline::Position() const
{
    return syncManager_ ? syncManager_->GetClock() / 1000.0 : 0.0;
}

bool MediaPipeline::IsInitialized() const
{
    return demuxer_ && Vdecoder_ && videoRender_ && positionTimer_ && demuxerTH_ && VdecoderTH_;
}

void MediaPipeline::SetPlaybackSpeed(double speed)
{
    if (audioplay_) {
        audioplay_->SetSpeed(speed);
    }
    if (videoRender_) {
        videoRender_->SetSpeed(speed);
    }
}

void MediaPipeline::SetVolume(double volume)
{
    const double clampedVolume = std::clamp(volume, 0.0, 1.0);
    if (audioplay_) {
        audioplay_->SetVolume(static_cast<qreal>(clampedVolume));
        return;
    }

    emit VolumeChanged(clampedVolume);
}

void MediaPipeline::SetMuted(bool muted)
{
    if (audioplay_) {
        audioplay_->SetMuted(muted);
        return;
    }

    emit MuteChanged(muted);
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

    connect(demuxer_.get(), &demuxer::DemuxErrorOccurred,
            this, &MediaPipeline::HandleDemuxError);

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
    APP_LOG_DEBUG("MediaPipeline release resources begin");

    if (positionTimer_) {
        APP_LOG_DEBUG("MediaPipeline release: stop position timer");
        positionTimer_->stop();
    }

    APP_LOG_DEBUG("MediaPipeline release: request queues stop");
    if (APacket_) { APacket_->RequestStop(); }
    if (VPacket_) { VPacket_->RequestStop(); }
    if (AFrame_)  { AFrame_->RequestStop();  }
    if (VFrame_)  { VFrame_->RequestStop();  }

    if (demuxer_) {
        APP_LOG_DEBUG("MediaPipeline release: stop demuxer");
        demuxer_->Stop();
    }
    if (Adecoder_) {
        APP_LOG_DEBUG("MediaPipeline release: stop audio decoder");
        Adecoder_->Stop();
    }
    if (Vdecoder_) {
        APP_LOG_DEBUG("MediaPipeline release: stop video decoder");
        Vdecoder_->Stop();
    }
    if (audioplay_) {
        APP_LOG_DEBUG("MediaPipeline release: stop audio player");
        audioplay_->Stop();
    }
    if (videoRender_) {
        APP_LOG_DEBUG("MediaPipeline release: stop video render");
        videoRender_->StopRendering();
    }

    auto stopThread = [](const char* name, std::unique_ptr<QThread>& thread) {
        if (thread) {
            APP_LOG_DEBUG("MediaPipeline release: quit thread", name,
                          "running =", thread->isRunning());
            thread->quit();
            APP_LOG_DEBUG("MediaPipeline release: wait thread begin", name);
            thread->wait();
            APP_LOG_DEBUG("MediaPipeline release: wait thread finished", name);
            thread.reset();
        }
    };
    stopThread("demuxer", demuxerTH_);
    stopThread("audio decoder", AdecoderTH_);
    stopThread("video decoder", VdecoderTH_);
    stopThread("audio player", audioplayTH_);

    APP_LOG_DEBUG("MediaPipeline release: reset infrastructure objects");
    demuxer_.reset();
    Adecoder_.reset();
    Vdecoder_.reset();
    audioplay_.reset();
    if (videoRender_) {
        APP_LOG_DEBUG("MediaPipeline release: delete video render");
        delete videoRender_;
        videoRender_ = nullptr;
    }

    APP_LOG_DEBUG("MediaPipeline release: reset queues and sync manager");
    APacket_.reset();
    VPacket_.reset();
    AFrame_.reset();
    VFrame_.reset();
    syncManager_.reset();
    if (mediaSource_) {
        APP_LOG_DEBUG("MediaPipeline release: close media source");
        mediaSource_->Close();
    }
    mediaSource_.reset();
    positionTimer_.reset();
    APP_LOG_DEBUG("MediaPipeline release resources finished");
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

void MediaPipeline::HandleDemuxError(const QString& message)
{
    if (demuxer_) {
        demuxer_->Stop();
    }
    if (Adecoder_) {
        Adecoder_->Stop();
    }
    if (Vdecoder_) {
        Vdecoder_->Stop();
    }

    if (APacket_) {
        APacket_->RequestStop();
    }
    if (VPacket_) {
        VPacket_->RequestStop();
    }
    if (AFrame_) {
        AFrame_->RequestStop();
    }
    if (VFrame_) {
        VFrame_->RequestStop();
    }
    if (positionTimer_) {
        positionTimer_->stop();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }
    if (videoRender_) {
        videoRender_->Pause();
    }

    emit ErrorOccurred(message.isEmpty() ? QString("Demuxer runtime error") : message);
}

bool MediaPipeline::SuspendPipelineAndWait()
{
    APP_LOG_DEBUG("MediaPipeline suspend begin");

    if (videoRender_) {
        APP_LOG_DEBUG("MediaPipeline suspend: pause video render");
        videoRender_->Pause();
    }
    if (audioplay_) {
        APP_LOG_DEBUG("MediaPipeline suspend: pause audio player");
        audioplay_->Pause();
    }

    bool suspended = true;
    if (demuxer_ && demuxerTH_ && demuxerTH_->isRunning()) {
        APP_LOG_DEBUG("MediaPipeline suspend: demuxer wait begin");
        suspended = demuxer_->SuspendAndWait() == 0 && suspended;
        APP_LOG_DEBUG("MediaPipeline suspend: demuxer wait finished", "result =", suspended);
    }
    if (Adecoder_ && AdecoderTH_ && AdecoderTH_->isRunning()) {
        APP_LOG_DEBUG("MediaPipeline suspend: audio decoder wait begin");
        suspended = Adecoder_->SuspendAndWait() == 0 && suspended;
        APP_LOG_DEBUG("MediaPipeline suspend: audio decoder wait finished", "result =", suspended);
    }
    if (Vdecoder_ && VdecoderTH_ && VdecoderTH_->isRunning()) {
        APP_LOG_DEBUG("MediaPipeline suspend: video decoder wait begin");
        suspended = Vdecoder_->SuspendAndWait() == 0 && suspended;
        APP_LOG_DEBUG("MediaPipeline suspend: video decoder wait finished", "result =", suspended);
    }

    APP_LOG_DEBUG("MediaPipeline suspend finished", "result =", suspended);
    return suspended;
}

void MediaPipeline::ResetPipelineToStart()
{
    if (videoRender_) {
        videoRender_->StopRendering();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }

    ClearQueues();
    demuxer_->SeekTo(0.0);
    FlushDecoders();
    ClearQueues();
    syncManager_->SeekTo(0.0);
}

void MediaPipeline::ResetRealtimePipelineStop()
{
    if (videoRender_) {
        videoRender_->StopRendering();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }

    ClearQueues();
    FlushDecoders();
    ClearQueues();
    syncManager_->SeekTo(0.0);
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
