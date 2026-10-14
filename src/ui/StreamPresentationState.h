#ifndef UI_STREAMPRESENTATIONSTATE_H
#define UI_STREAMPRESENTATIONSTATE_H

#include <QMetaType>
#include <QString>
#include <QtCore/Qt>

/**
 * @brief StreamSourceKind 视频源在展示层中的来源类型
 *
 * 该类型只描述 UI 已知的创建入口，不持有媒体源或播放管线对象。
 */
enum class StreamSourceKind {
    Unknown,
    File,
    Camera,
    Rtsp
};

/**
 * @brief StreamPresentationMetadata 主窗口为流维护的稳定展示元数据
 */
struct StreamPresentationMetadata {
    QString displayName;
    StreamSourceKind sourceKind = StreamSourceKind::Unknown;
    bool isRealtime = false;
    QString lastError;
};

/**
 * @brief StreamRuntimePresentationMetrics UI 低频采样后的单路运行指标
 * @details 有效性与数值分离，确保 0 FPS 和 0% 丢帧仍可被正确展示。
 */
struct StreamRuntimePresentationMetrics {
    bool hasFrameSize = false;
    int frameWidth = 0;
    int frameHeight = 0;
    bool hasPreviewFps = false;
    double previewFps = 0.0;
    bool hasSchedulerDropRate = false;
    double schedulerDropRate = 0.0;
    QString videoCodecName;
    bool schedulerRunning = false;
    bool imageProcessingEnabled = false;
    bool visionAnalysisActive = false;
};

/**
 * @brief StreamPresentationState 单次列表刷新使用的只读展示快照
 */
struct StreamPresentationState {
    int streamId = -1;
    QString displayName;
    StreamSourceKind sourceKind = StreamSourceKind::Unknown;
    int slotIndex = -1;
    bool hasAudio = false;
    bool isActiveVideo = false;
    bool isActiveAudio = false;
    bool isRealtime = false;
    QString lastError;
    StreamRuntimePresentationMetrics runtimeMetrics;
};

inline constexpr int kStreamPresentationStateRole = Qt::UserRole + 1;

Q_DECLARE_METATYPE(StreamPresentationState)

#endif // UI_STREAMPRESENTATIONSTATE_H
