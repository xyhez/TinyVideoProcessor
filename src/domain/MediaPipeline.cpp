#include "MediaPipeline.h"
#include "tiny_log/Logging.h"
#include "infrastructure/vision/imageprocessingframesink.h"
#include "infrastructure/vision/visionpipelinemanager.h"
#include "infrastructure/vision/visionprocessingframesink.h"

#include <algorithm>
#include <QVBoxLayout>

namespace {
constexpr size_t kPacketQueueMax = 30;	///< 音视频包队列最大缓存数量，限制解复用端超前读取
constexpr size_t kFrameQueueMax = 30;	///< 音视频帧队列最大缓存数量，限制解码端超前输出
}

MediaPipeline::MediaPipeline(VisionPipelineManager* visionPipelineManager, QObject* parent)
    : QObject(parent)
    , visionPipelineManager_(visionPipelineManager)
{
}

MediaPipeline::~MediaPipeline()
{
    Shutdown();
}

bool MediaPipeline::Initialize(std::shared_ptr<IMediaSource> mediaSource,
                               QWidget* rendererContainer,
                               IVideoFrameSink* videoFrameSink)
{
    shuttingDown_ = false;

    if (!mediaSource) {
        LOG_CRITICAL("MediaPipeline initialize failed: media source is null");
        return false;
    }

    if (!CreatePipelineResources(mediaSource, rendererContainer, videoFrameSink)) {
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
    if (playbackFinished_) {
        LOG_WARN("MediaPipeline start rejected: playback has finished, recreate pipeline to replay");
        return;
    }

    if (!demuxer_ || !Vdecoder_ || !videoFrameScheduler_ || !activeVideoFrameSink_ ||
        !positionTimer_ || !demuxerTH_ || !VdecoderTH_) {
        LOG_WARN("MediaPipeline start failed: playback pipeline is not initialized");
        emit ErrorOccurred("Playback pipeline is not initialized");
        return;
    }

    const bool hasAudio = Adecoder_ || audioplay_ || AdecoderTH_ || audioplayTH_;
    if (hasAudio && (!Adecoder_ || !audioplay_ || !AdecoderTH_ || !audioplayTH_)) {
        LOG_WARN("MediaPipeline start failed: audio pipeline is incomplete");
        emit ErrorOccurred("Audio pipeline is incomplete");
        return;
    }

    positionTimer_->start();
    emit PositionChanged(Position());
    emit DurationChanged(Duration());

    if (mediaSource_ && mediaSource_->IsRealtime()) {
        mediaSource_->ClearInterrupt();
    }

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
    videoFrameScheduler_->Start();
}

void MediaPipeline::Pause()
{
    if (!demuxer_ || !Vdecoder_ || !videoFrameScheduler_ || !positionTimer_) {
        LOG_WARN("MediaPipeline pause failed: playback pipeline is not initialized");
        emit ErrorOccurred("Playback pipeline is not initialized");
        return;
    }

    emit PositionChanged(Position());
    demuxer_->Pause();
    if (mediaSource_ && mediaSource_->IsRealtime()) {
        mediaSource_->RequestInterrupt();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }
    if (Adecoder_) {
        Adecoder_->Pause();
    }
    Vdecoder_->Pause();
    videoFrameScheduler_->Pause();
    positionTimer_->stop();
}

void MediaPipeline::Stop()
{
    if (!demuxer_ || !Vdecoder_ || !syncManager_ || !positionTimer_) {
        return;
    }

    if (playbackFinished_) {
        emit PositionChanged(0.0);
        positionTimer_->stop();
        if (videoFrameScheduler_) {
            videoFrameScheduler_->Stop();
            videoFrameScheduler_->ClearOutputFrame();
        }
        if (audioplay_) {
            audioplay_->Pause();
        }
        ClearQueues();
        syncManager_->SeekTo(0.0);
        return;
    }

    const bool realtimeSource = mediaSource_ && mediaSource_->IsRealtime();
    if (realtimeSource) {
        mediaSource_->RequestInterrupt();
    }

    if (!SuspendPipelineAndWait()) {
        if (realtimeSource) {
            mediaSource_->ClearInterrupt();
        }
        LOG_WARN("MediaPipeline stop failed: playback pipeline suspend failed");
        emit ErrorOccurred("Playback pipeline suspend failed");
        return;
    }

    if (realtimeSource) {
        mediaSource_->ClearInterrupt();
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
        LOG_WARN("MediaPipeline seek failed: playback pipeline is not initialized");
        emit ErrorOccurred("Playback pipeline is not initialized");
        return false;
    }

    if (playbackFinished_) {
        LOG_WARN("MediaPipeline seek rejected: playback has finished, recreate pipeline to seek");
        return false;
    }

    // 检查媒体源是否支持 seek
    if (mediaSource_ && !mediaSource_->IsSeekable()) {
        LOG_WARN("MediaPipeline seek rejected: media source is not seekable");
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
        LOG_WARN("MediaPipeline seek failed: playback pipeline suspend failed");
        emit ErrorOccurred("Playback pipeline suspend failed");
        return false;
    }

    if (videoFrameScheduler_) {
        videoFrameScheduler_->ClearOutputFrame();
    }

    ClearQueues();

    const int seekResult = demuxer_->SeekTo(targetSeconds);
    if (seekResult != 0) {
        LOG_WARN("MediaPipeline seek failed: demuxer rejected target", targetSeconds);
        emit ErrorOccurred("Demuxer rejected seek target");
        return false;
    }

    FlushDecoders();
    Vdecoder_->SetDiscardBefore(targetSeconds);
    ClearQueues();
    ResumeQueues();
    ResetPlaybackCompletionState();
    syncManager_->SeekTo(targetSeconds * 1000.0);

    emit PositionChanged(Position());

    return true;
}

bool MediaPipeline::SeekRelative(double seconds)
{
    if (!demuxer_) {
        LOG_WARN("MediaPipeline seek failed: demuxer is not initialized");
        emit ErrorOccurred("Demuxer is not initialized");
        return false;
    }

    // 检查媒体源是否支持 seek
    if (mediaSource_ && !mediaSource_->IsSeekable()) {
        LOG_WARN("MediaPipeline seek rejected: media source is not seekable");
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
    return demuxer_ && Vdecoder_ && videoFrameScheduler_ && activeVideoFrameSink_ &&
           positionTimer_ && demuxerTH_ && VdecoderTH_;
}

MediaPipeline::RuntimeMetricsSnapshot MediaPipeline::RuntimeMetrics() const
{
    RuntimeMetricsSnapshot snapshot;
    snapshot.initialized = IsInitialized();
    snapshot.frameWidth = sourceVideoWidth_;
    snapshot.frameHeight = sourceVideoHeight_;
    snapshot.videoCodecId = sourceVideoCodecId_;
    snapshot.imageProcessingEnabled = IsImageProcessingEnabled();
    snapshot.visionAnalysisActive = IsVisionActive();

    if (!videoFrameScheduler_) {
        return snapshot;
    }

    const VideoFrameScheduler::RuntimeMetricsSnapshot schedulerMetrics =
        videoFrameScheduler_->RuntimeMetrics();
    snapshot.submittedFrames = schedulerMetrics.submittedFrames;
    snapshot.schedulerDroppedFrames = schedulerMetrics.schedulerDroppedFrames;
    snapshot.schedulerRunning = schedulerMetrics.running;
    if (schedulerMetrics.frameWidth > 0 && schedulerMetrics.frameHeight > 0) {
        snapshot.frameWidth = schedulerMetrics.frameWidth;
        snapshot.frameHeight = schedulerMetrics.frameHeight;
    }
    return snapshot;
}

void MediaPipeline::SetPlaybackSpeed(double speed)
{
    if (audioplay_) {
        audioplay_->SetSpeed(speed);
    }
    if (videoFrameScheduler_) {
        videoFrameScheduler_->SetSpeed(speed);
    }
}

void MediaPipeline::SetPreviewFrameRateReductionEnabled(bool enabled, int targetFps)
{
    if (videoFrameScheduler_) {
        videoFrameScheduler_->SetPreviewFrameRateReductionEnabled(enabled, targetFps);
    }
}

void MediaPipeline::SetImageProcessingEnabled(bool enabled)
{
    if (imageProcessingFrameSink_) {
        imageProcessingFrameSink_->SetImageProcessingEnabled(enabled);
    }
}

bool MediaPipeline::IsImageProcessingEnabled() const
{
    return imageProcessingFrameSink_ && imageProcessingFrameSink_->IsImageProcessingEnabled();
}

ImageProcessorPipeline* MediaPipeline::ImageProcessingPipeline()
{
    return imageProcessingFrameSink_ ? imageProcessingFrameSink_->ImageProcessingPipeline() : nullptr;
}

const ImageProcessorPipeline* MediaPipeline::ImageProcessingPipeline() const
{
    return imageProcessingFrameSink_ ? imageProcessingFrameSink_->ImageProcessingPipeline() : nullptr;
}

void MediaPipeline::SetVisionActive(bool active)
{
    if (visionProcessingFrameSink_) {
        visionProcessingFrameSink_->SetActive(active);
    }
}

bool MediaPipeline::IsVisionActive() const
{
    return visionProcessingFrameSink_ && visionProcessingFrameSink_->IsActive();
}

void MediaPipeline::SetVisionOverlayEnabled(bool enabled)
{
    if (visionProcessingFrameSink_) {
        visionProcessingFrameSink_->SetOverlayEnabled(enabled);
    }
}

bool MediaPipeline::IsVisionOverlayEnabled() const
{
    return visionProcessingFrameSink_ && visionProcessingFrameSink_->IsOverlayEnabled();
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
                                            QWidget* rendererContainer,
                                            IVideoFrameSink* videoFrameSink)
{
    mediaSource_ = mediaSource;

    VPacket_ = std::make_shared<SyncQueue<AVPacketPtr>>(kPacketQueueMax);
    VFrame_ = std::make_shared<SyncQueue<AVFramePtr>>(kFrameQueueMax);

    demuxer_ = std::make_unique<Demuxer>(mediaSource_, APacket_, VPacket_, AFrame_, VFrame_);

    const Fileinfo& sourceInfo = mediaSource_->GetSourceInfo();
    sourceVideoWidth_ = std::max(0, sourceInfo.width_);
    sourceVideoHeight_ = std::max(0, sourceInfo.height_);
    sourceVideoCodecId_ = sourceInfo.videoCodecId_ != AV_CODEC_ID_NONE
        ? sourceInfo.videoCodecId_
        : sourceInfo.CodecId_;
    const bool hasAudio = sourceInfo.hasAudio_;
    if (hasAudio) {
        APacket_ = std::make_shared<SyncQueue<AVPacketPtr>>(kPacketQueueMax);
        AFrame_ = std::make_shared<SyncQueue<AVFramePtr>>(kFrameQueueMax);
        demuxer_->SetAudioQueues(APacket_, AFrame_);
    }

    Vdecoder_ = std::make_unique<Decoder>();
    if (hasAudio) {
        Adecoder_ = std::make_unique<Decoder>();
        audioplay_ = std::make_unique<AudioPlayer>();
    }

    syncManager_ = std::make_shared<SyncManager>();
    syncManager_->SetSyncType(hasAudio ? SYNCTYPE::AUDIO : SYNCTYPE::VIDEO);
    videoFrameScheduler_ = std::make_unique<VideoFrameScheduler>();
    positionTimer_ = std::make_unique<QTimer>();
    positionTimer_->setInterval(200);

    demuxerTH_ = std::make_unique<QThread>();
    VdecoderTH_ = std::make_unique<QThread>();
    if (hasAudio) {
        AdecoderTH_ = std::make_unique<QThread>();
        audioplayTH_ = std::make_unique<QThread>();
    }

    if (hasAudio) {
        const int audioDecoderInit =
            Adecoder_->InitDecoder(APacket_, AFrame_, demuxer_->ReAcoderPar(), demuxer_->ReAtimebase());
        if (audioDecoderInit != 0) {
            LOG_CRITICAL("MediaPipeline create resources failed: audio decoder initialization failed");
            return false;
        }
    }

    const int videoDecoderInit =
        Vdecoder_->InitDecoder(VPacket_, VFrame_, demuxer_->ReVcoderPar(), demuxer_->ReVtimebase());
    if (videoDecoderInit != 0) {
        LOG_CRITICAL("MediaPipeline create resources failed: video decoder initialization failed");
        return false;
    }

    if (hasAudio) {
        audioplay_->InitAudioPlay(demuxer_->ReAcoderPar(), AFrame_, syncManager_, sourceInfo.A_timeBase);
    }
    const bool useDefaultVisibleRenderer = videoFrameSink == nullptr;
    if (useDefaultVisibleRenderer) {
        videoRender_ = new VideoRender(rendererContainer);
        if (rendererContainer) {
            if (!rendererContainer->layout()) {
                rendererContainer->setLayout(new QVBoxLayout());
            }
            rendererContainer->layout()->addWidget(videoRender_);
        }
    }

    IVideoFrameSink* downstreamSink = videoFrameSink ? videoFrameSink : videoRender_;
    if (!downstreamSink) {
        LOG_CRITICAL("MediaPipeline create resources failed: video frame sink is null");
        return false;
    }

    // 视觉分析 sink 插入到最下游 sink 之前
    if (visionPipelineManager_) {
        visionProcessingFrameSink_ = std::make_unique<VisionProcessingFrameSink>(
            visionPipelineManager_, downstreamSink);
        downstreamSink = visionProcessingFrameSink_.get();
    }

    imageProcessingFrameSink_ = std::make_unique<ImageProcessingFrameSink>();
    imageProcessingFrameSink_->SetDownstreamSink(downstreamSink);
    activeVideoFrameSink_ = imageProcessingFrameSink_.get();

    videoFrameScheduler_->Initialize(VFrame_, syncManager_, sourceInfo.FrameRate, sourceInfo.V_timeBase);
    videoFrameScheduler_->SetFrameSink(activeVideoFrameSink_);

    demuxer_->moveToThread(demuxerTH_.get());
    Vdecoder_->moveToThread(VdecoderTH_.get());
    if (hasAudio) {
        Adecoder_->moveToThread(AdecoderTH_.get());
        audioplay_->moveToThread(audioplayTH_.get());
    }

    ResetPlaybackCompletionState();

    return true;
}

void MediaPipeline::CreateConnects()
{
    connect(demuxerTH_.get(), &QThread::started,
            demuxer_.get(), &Demuxer::StartDemuxer);

    if (AdecoderTH_ && Adecoder_) {
        connect(AdecoderTH_.get(), &QThread::started,
                Adecoder_.get(), &Decoder::StartDecoder);
    }

    connect(VdecoderTH_.get(), &QThread::started,
            Vdecoder_.get(), &Decoder::StartDecoder);

    if (audioplayTH_ && audioplay_) {
        connect(audioplayTH_.get(), &QThread::started,
                audioplay_.get(), &AudioPlayer::StartAudioPlay);
    }

    connect(demuxer_.get(), &Demuxer::DemuxFinished,
            demuxerTH_.get(), &QThread::quit, Qt::DirectConnection);
    connect(demuxer_.get(), &Demuxer::DemuxErrorOccurred,
            demuxerTH_.get(), &QThread::quit, Qt::DirectConnection);
    connect(Vdecoder_.get(), &Decoder::DecoderFinished,
            VdecoderTH_.get(), &QThread::quit, Qt::DirectConnection);
    connect(Vdecoder_.get(), &Decoder::DecoderErrorOccurred,
            VdecoderTH_.get(), &QThread::quit, Qt::DirectConnection);
    if (Adecoder_ && AdecoderTH_) {
        connect(Adecoder_.get(), &Decoder::DecoderFinished,
                AdecoderTH_.get(), &QThread::quit, Qt::DirectConnection);
        connect(Adecoder_.get(), &Decoder::DecoderErrorOccurred,
                AdecoderTH_.get(), &QThread::quit, Qt::DirectConnection);
    }
    if (audioplay_ && audioplayTH_) {
        connect(audioplay_.get(), &AudioPlayer::AudioPlaybackDrained,
                audioplayTH_.get(), &QThread::quit, Qt::DirectConnection);
        connect(audioplay_.get(), &AudioPlayer::AudioPlaybackError,
                audioplayTH_.get(), &QThread::quit, Qt::DirectConnection);
    }

    connect(demuxer_.get(), &Demuxer::DemuxFinished,
            this, &MediaPipeline::HandleDemuxFinished);

    connect(demuxer_.get(), &Demuxer::DemuxErrorOccurred,
            this, &MediaPipeline::HandleDemuxError);
    connect(Vdecoder_.get(), &Decoder::DecoderErrorOccurred,
            this, &MediaPipeline::HandleDemuxError);
    if (Adecoder_) {
        connect(Adecoder_.get(), &Decoder::DecoderErrorOccurred,
                this, &MediaPipeline::HandleDemuxError);
    }

    connect(videoFrameScheduler_.get(), &VideoFrameScheduler::VideoPlaybackDrained,
            this, &MediaPipeline::HandleVideoPlaybackDrained);

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

        connect(audioplay_.get(), &AudioPlayer::AudioPlaybackDrained,
                this, &MediaPipeline::HandleAudioPlaybackDrained);
        connect(audioplay_.get(), &AudioPlayer::AudioPlaybackError,
                this, &MediaPipeline::HandleDemuxError);
    }
}

void MediaPipeline::ReleaseResources()
{
    shuttingDown_ = true;
    LOG_DEBUG("MediaPipeline release resources begin");

    if (positionTimer_) {
        LOG_DEBUG("MediaPipeline release: stop position timer");
        positionTimer_->stop();
    }

    if (mediaSource_) {
        LOG_DEBUG("MediaPipeline release: request media source interrupt");
        mediaSource_->RequestInterrupt();
    }

    LOG_DEBUG("MediaPipeline release: request queues stop");
    if (APacket_) { APacket_->RequestStop(); }
    if (VPacket_) { VPacket_->RequestStop(); }
    if (AFrame_)  { AFrame_->RequestStop();  }
    if (VFrame_)  { VFrame_->RequestStop();  }

    if (demuxer_) {
        LOG_DEBUG("MediaPipeline release: stop demuxer");
        demuxer_->Stop();
    }
    if (Adecoder_) {
        LOG_DEBUG("MediaPipeline release: stop audio decoder");
        Adecoder_->Stop();
    }
    if (Vdecoder_) {
        LOG_DEBUG("MediaPipeline release: stop video decoder");
        Vdecoder_->Stop();
    }
    if (audioplay_) {
        LOG_DEBUG("MediaPipeline release: stop audio player");
        audioplay_->Stop();
    }
    if (videoFrameScheduler_) {
        LOG_DEBUG("MediaPipeline release: stop video frame scheduler");
        videoFrameScheduler_->Stop();
        videoFrameScheduler_->ClearOutputFrame();
        videoFrameScheduler_->SetFrameSink(nullptr);
    }
    if (imageProcessingFrameSink_) {
        imageProcessingFrameSink_->SetDownstreamSink(nullptr);
    }
    if (visionProcessingFrameSink_) {
        visionProcessingFrameSink_->SetDownstreamSink(nullptr);
    }

    auto stopThread = [](const char* name, std::unique_ptr<QThread>& thread) {
        if (thread) {
            LOG_DEBUG("MediaPipeline release: quit thread", name,
                          "running =", thread->isRunning());
            thread->quit();
            LOG_DEBUG("MediaPipeline release: wait thread begin", name);
            thread->wait();
            LOG_DEBUG("MediaPipeline release: wait thread finished", name);
            thread.reset();
        }
    };
    stopThread("demuxer", demuxerTH_);
    stopThread("audio decoder", AdecoderTH_);
    stopThread("video decoder", VdecoderTH_);
    stopThread("audio player", audioplayTH_);

    LOG_DEBUG("MediaPipeline release: reset infrastructure objects");
    demuxer_.reset();
    Adecoder_.reset();
    Vdecoder_.reset();
    audioplay_.reset();
    videoFrameScheduler_.reset();
    imageProcessingFrameSink_.reset();
    visionProcessingFrameSink_.reset();
    if (videoRender_) {
        LOG_DEBUG("MediaPipeline release: delete video render");
        delete videoRender_;
        videoRender_ = nullptr;
    }
    activeVideoFrameSink_ = nullptr;

    LOG_DEBUG("MediaPipeline release: reset queues and sync manager");
    APacket_.reset();
    VPacket_.reset();
    AFrame_.reset();
    VFrame_.reset();
    syncManager_.reset();
    if (mediaSource_) {
        LOG_DEBUG("MediaPipeline release: close media source");
        mediaSource_->Close();
    }
    mediaSource_.reset();
    sourceVideoWidth_ = 0;
    sourceVideoHeight_ = 0;
    sourceVideoCodecId_ = AV_CODEC_ID_NONE;
    positionTimer_.reset();
    LOG_DEBUG("MediaPipeline release resources finished");
}

void MediaPipeline::HandleDemuxFinished()
{
    if (shuttingDown_ || !VPacket_ || !VPacket_->IsEndOfStream()) {
        return;
    }
    if (APacket_ && !APacket_->IsEndOfStream()) {
        return;
    }

    demuxEndOfStream_ = true;
    TryFinalizePlayback();
}

void MediaPipeline::HandleAudioPlaybackDrained()
{
    if (shuttingDown_ || !AFrame_ || !AFrame_->IsEndOfStream() || !AFrame_->Empty()) {
        return;
    }

    audioPlaybackDrained_ = true;

    // 音频主时钟停止后，剩余视频帧改由视频时钟按既有 cadence 收尾，
    // 避免最后几帧永远等待一个不再前进的音频时钟。
    if (syncManager_) {
        syncManager_->SetSyncType(SYNCTYPE::VIDEO);
    }

    TryFinalizePlayback();
}

void MediaPipeline::HandleVideoPlaybackDrained()
{
    if (shuttingDown_ || !VFrame_ || !VFrame_->IsEndOfStream() || !VFrame_->Empty()) {
        return;
    }

    videoPlaybackDrained_ = true;
    TryFinalizePlayback();
}

void MediaPipeline::TryFinalizePlayback()
{
    const bool audioRequired = Adecoder_ != nullptr;
    if (shuttingDown_ || playbackFinished_ || !demuxEndOfStream_ ||
        !videoPlaybackDrained_ || (audioRequired && !audioPlaybackDrained_)) {
        return;
    }

    playbackFinished_ = true;
    if (positionTimer_) {
        positionTimer_->stop();
    }

    emit PositionChanged(Duration());
    emit PlaybackFinished();
}

void MediaPipeline::ResetPlaybackCompletionState()
{
    playbackFinished_ = false;
    demuxEndOfStream_ = false;
    audioPlaybackDrained_ = Adecoder_ == nullptr;
    videoPlaybackDrained_ = false;
    if (syncManager_) {
        syncManager_->SetSyncType(Adecoder_ ? SYNCTYPE::AUDIO : SYNCTYPE::VIDEO);
    }
}

void MediaPipeline::HandleDemuxError(const QString& message)
{
    if (shuttingDown_) {
        return;
    }

    ResetPlaybackCompletionState();

    if (mediaSource_ && mediaSource_->IsRealtime()) {
        mediaSource_->RequestInterrupt();
    }

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
    if (videoFrameScheduler_) {
        videoFrameScheduler_->Pause();
        videoFrameScheduler_->ClearOutputFrame();
    }

    emit ErrorOccurred(message.isEmpty() ? QString("Demuxer runtime error") : message);
}

bool MediaPipeline::SuspendPipelineAndWait()
{
    LOG_DEBUG("MediaPipeline suspend begin");

    if (videoFrameScheduler_) {
        LOG_DEBUG("MediaPipeline suspend: pause video frame scheduler");
        videoFrameScheduler_->Pause();
    }
    if (audioplay_) {
        LOG_DEBUG("MediaPipeline suspend: pause audio player");
        audioplay_->Pause();
    }

    bool suspended = true;
    if (demuxer_ && demuxerTH_ && demuxerTH_->isRunning()) {
        LOG_DEBUG("MediaPipeline suspend: demuxer wait begin");
        suspended = demuxer_->SuspendAndWait() == 0 && suspended;
        LOG_DEBUG("MediaPipeline suspend: demuxer wait finished", "result =", suspended);
    }
    if (Adecoder_ && AdecoderTH_ && AdecoderTH_->isRunning()) {
        LOG_DEBUG("MediaPipeline suspend: audio decoder wait begin");
        suspended = Adecoder_->SuspendAndWait() == 0 && suspended;
        LOG_DEBUG("MediaPipeline suspend: audio decoder wait finished", "result =", suspended);
    }
    if (Vdecoder_ && VdecoderTH_ && VdecoderTH_->isRunning()) {
        LOG_DEBUG("MediaPipeline suspend: video decoder wait begin");
        suspended = Vdecoder_->SuspendAndWait() == 0 && suspended;
        LOG_DEBUG("MediaPipeline suspend: video decoder wait finished", "result =", suspended);
    }

    LOG_DEBUG("MediaPipeline suspend finished", "result =", suspended);
    return suspended;
}

void MediaPipeline::ResetPipelineToStart()
{
    if (videoFrameScheduler_) {
        videoFrameScheduler_->Stop();
        videoFrameScheduler_->ClearOutputFrame();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }

    ClearQueues();
    demuxer_->SeekTo(0.0);
    FlushDecoders();
    ClearQueues();
    ResumeQueues();
    ResetPlaybackCompletionState();
    syncManager_->SeekTo(0.0);
}

void MediaPipeline::ResetRealtimePipelineStop()
{
    if (videoFrameScheduler_) {
        videoFrameScheduler_->Stop();
        videoFrameScheduler_->ClearOutputFrame();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }

    ClearQueues();
    FlushDecoders();
    ClearQueues();
    ResumeQueues();
    ResetPlaybackCompletionState();
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

void MediaPipeline::ResumeQueues()
{
    if (APacket_) { APacket_->Resume(); }
    if (VPacket_) { VPacket_->Resume(); }
    if (AFrame_)  { AFrame_->Resume();  }
    if (VFrame_)  { VFrame_->Resume();  }
}
