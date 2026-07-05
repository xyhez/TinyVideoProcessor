#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include <memory>

#include "application/MediaPlayController.h"
#include "domain/PlaylistManager.h"

class MultiViewRenderer;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief MainWindow 构造函数
     * @param parent Qt 父窗口
     */
    MainWindow(QWidget *parent = nullptr);

    /**
     * @brief MainWindow 析构函数
     */
    ~MainWindow();

    /**
     * @brief CreateConnects 创建 UI 与 Controller 的信号槽连接
     */
    void CreateConnects();

    /**
     * @brief closeEvent 窗口关闭事件，负责退出前释放播放资源
     * @param event 关闭事件
     */
    void closeEvent(QCloseEvent *event) override;

    /**
     * @brief keyReleaseEvent 键盘释放事件，处理播放快捷键
     * @param ev 键盘事件
     */
    void keyReleaseEvent(QKeyEvent *ev) override;

    /**
     * @brief mouseMoveEvent 鼠标移动事件，全屏时恢复鼠标显示
     * @param event 鼠标事件
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief eventFilter 捕获子控件鼠标移动事件
     * @param watched 事件来源对象
     * @param event 事件对象
     * @return true 事件已处理
     * @return false 继续默认事件处理
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief Start 转发播放命令
     */
    void Start();

    /**
     * @brief Stop 转发停止命令
     */
    void Stop();

    /**
     * @brief Pause 转发暂停命令
     */
    void Pause();

    /**
     * @brief VolumeUp 转发增大音量命令
     */
    void VolumeUp();

    /**
     * @brief VolumeDown 转发减小音量命令
     */
    void VolumeDown();

    /**
     * @brief Rewind 转发后退命令
     */
    void Rewind();

    /**
     * @brief Forward 转发前进命令
     */
    void Forward();

private slots:
    /**
     * @brief on_pushButton_play_clicked 播放按钮点击槽
     */
    void on_pushButton_play_clicked();

    /**
     * @brief on_pushButton_pause_clicked 暂停按钮点击槽
     */
    void on_pushButton_pause_clicked();

private:
    static constexpr int kMaxVideoSlotCount = 16;	///< 主窗口支持的最大逻辑视频 slot 数

    enum class MultiViewLayout {
        Single,
        Grid2x2,
        Grid3x3,
        Grid4x4
    };

    Ui::MainWindow *ui;	///< UI 对象

    std::unique_ptr<MediaPlayController> mediaPlayController_ = nullptr;	///< 播放控制器
    std::unique_ptr<PlaylistManager> playlistManager_ = nullptr;	///< 播放列表管理器
    std::unique_ptr<QTimer> cursorHideTimer_ = nullptr;	///< 全屏鼠标隐藏定时器
    MultiViewRenderer* multiViewRenderer_ = nullptr;	///< 统一多画面渲染器

    /**
     * @brief UpdatePlaybackControls 根据播放状态更新按钮可用状态
     * @param state 当前播放状态
     */
    void UpdatePlaybackControls(MediaPlayController::PlaybackState state);

    /**
     * @brief OpenFile 打开文件选择框并添加文件到播放列表
     */
    void OpenFile();

    /**
     * @brief RemoveSelectedPlaylistItem 删除当前选中的播放列表条目
     */
    void RemoveSelectedPlaylistItem();

    /**
     * @brief ClearPlaylist 清空播放列表
     */
    void ClearPlaylist();

    /**
     * @brief InitializePlayModeComboBox 初始化播放模式下拉框
     */
    void InitializePlayModeComboBox();

    /**
     * @brief InitializeVolumeControls 初始化音量控件状态
     */
    void InitializeVolumeControls();

    /**
     * @brief InitializePlaybackSpeedComboBox 初始化播放速度下拉框
     */
    void InitializePlaybackSpeedComboBox();

    /**
     * @brief InitializePreviewFrameRateControls 初始化预览降帧单选控件
     */
    void InitializePreviewFrameRateControls();

    /**
     * @brief InitializeImageProcessingControls 初始化图像处理控件
     */
    void InitializeImageProcessingControls();

    /**
     * @brief InitializeVisionControls 初始化视觉分析控件
     */
    void InitializeVisionControls();

    /**
     * @brief InitializeCursorHideTimer 初始化全屏鼠标隐藏定时器
     */
    void InitializeCursorHideTimer();

    /**
     * @brief InitializeVideoArea 初始化主视频显示区域事件状态
     */
    void InitializeVideoArea();

    /**
     * @brief InitializeMultiViewLayout 初始化多路显示布局状态
     */
    void InitializeMultiViewLayout();

    /**
     * @brief InitializeMultiViewRenderer 初始化主视频区域统一多画面渲染器
     */
    void InitializeMultiViewRenderer();

    /**
     * @brief RefreshMultiViewRenderer 同步统一渲染器的布局、slot 和 active stream
     */
    void RefreshMultiViewRenderer();

    /**
     * @brief CreateMultiViewFrameSinkFactory 创建统一渲染器帧 sink 工厂
     * @return 可用时返回按 StreamId 创建 adapter 的工厂，否则返回空工厂
     */
    MediaPlayController::FrameSinkFactory CreateMultiViewFrameSinkFactory() const;

    /**
     * @brief MultiViewRendererViewSlots 获取统一渲染器 slot 与 stream 的绑定关系
     * @return slot 索引到 StreamId 的映射
     */
    QMap<int, MediaPlayController::StreamId> MultiViewRendererViewSlots() const;

    /**
     * @brief IsMultiViewFocused 判断当前是否处于单路聚焦显示状态
     * @return true 已聚焦单路
     * @return false 常规多路布局
     */
    bool IsMultiViewFocused() const;

    /**
     * @brief ToggleFocusedStreamView 切换指定流的单路聚焦显示状态
     * @param streamId 目标流 ID
     */
    void ToggleFocusedStreamView(MediaPlayController::StreamId streamId);

    /**
     * @brief ExitFocusedStreamView 退出单路聚焦显示状态
     */
    void ExitFocusedStreamView();

    /**
     * @brief SetMultiViewLayout 切换多路显示布局状态
     * @param layout 目标布局
     */
    void SetMultiViewLayout(MultiViewLayout layout);

    /**
     * @brief ShowGrid4x4CapacityNoticeIfNeeded 必要时提示 4x4 模式的性能边界
     */
    void ShowGrid4x4CapacityNoticeIfNeeded();

    /**
     * @brief CurrentMultiViewLayout 获取当前多路显示布局
     * @return 当前布局
     */
    MultiViewLayout CurrentMultiViewLayout() const;

    /**
     * @brief MaxVisibleStreamsForLayout 获取指定布局最大可见路数
     * @param layout 目标布局
     * @return 最大可见路数
     */
    int MaxVisibleStreamsForLayout(MultiViewLayout layout) const;

    /**
     * @brief MaxVisibleStreams 获取当前布局最大可见路数
     * @return 最大可见路数
     */
    int MaxVisibleStreams() const;

    /**
     * @brief MaxVideoSlots 获取主窗口可用逻辑视频 slot 数量
     * @return 最大逻辑 slot 数量
     */
    int MaxVideoSlots() const;

    /**
     * @brief CurrentVideoSlotCapacity 获取当前布局允许分配的逻辑视频 slot 数量
     * @return 当前布局可分配 slot 数量
     */
    int CurrentVideoSlotCapacity() const;

    /**
     * @brief FallbackRendererContainer 获取默认 VideoRender fallback 容器
     * @return fallback 容器
     */
    QWidget* FallbackRendererContainer() const;

    /**
     * @brief FirstAvailableVideoSlotIndex 获取第一个空闲逻辑视频 slot
     * @return 空闲 slot 索引，无空闲 slot 时返回 -1
     */
    int FirstAvailableVideoSlotIndex() const;

    /**
     * @brief BindStreamToVideoSlot 绑定流和逻辑视频 slot
     * @param streamId 流 ID
     * @param slotIndex slot 索引
     */
    void BindStreamToVideoSlot(MediaPlayController::StreamId streamId,
                               int slotIndex);

    /**
     * @brief ReleaseVideoSlotForStream 释放流占用的逻辑视频 slot
     * @param streamId 流 ID
     */
    void ReleaseVideoSlotForStream(MediaPlayController::StreamId streamId);

    /**
     * @brief SlotIndexForStream 获取流绑定的逻辑视频 slot 索引
     * @param streamId 流 ID
     * @return slot 索引，未绑定时返回 -1
     */
    int SlotIndexForStream(MediaPlayController::StreamId streamId) const;

    /**
     * @brief IsValidVideoSlotIndex 判断逻辑视频 slot 索引是否有效
     * @param slotIndex slot 索引
     * @return true 有效
     * @return false 无效
     */
    bool IsValidVideoSlotIndex(int slotIndex) const;

    /**
     * @brief ActivateStreamFromView 根据视图点击结果切换活动流
     * @param streamId 被点击的流 ID
     * @return true 点击已处理
     */
    bool ActivateStreamFromView(MediaPlayController::StreamId streamId);

    /**
     * @brief RefreshMultiViewLayout 根据当前布局刷新统一渲染器显示状态
     */
    void RefreshMultiViewLayout();

    /**
     * @brief UpdateLayoutComboBox 根据当前布局同步布局下拉框
     * @param layout 当前布局
     */
    void UpdateLayoutComboBox(MultiViewLayout layout);

    /**
     * @brief UpdatePlaybackSpeedComboBox 根据当前速度同步播放速度下拉框
     * @param speed 当前播放速度
     */
    void UpdatePlaybackSpeedComboBox(double speed);

    /**
     * @brief UpdateImageProcessingModeComboBox 根据当前图像处理模式同步下拉框
     * @param mode 当前图像处理模式
     */
    void UpdateImageProcessingModeComboBox(MediaPlayController::ImageProcessingMode mode);

    /**
     * @brief UpdateImageProcessingControls 根据 active stream 和当前模式更新图像处理控件状态
     */
    void UpdateImageProcessingControls();

    /**
     * @brief UpdateVisionPipelineComboBox 根据 active pipeline id 同步下拉框选中项
     */
    void UpdateVisionPipelineComboBox(const QString& activeId);

    /**
     * @brief UpdateVisionControls 根据 active stream 和 active pipeline 更新视觉分析控件状态
     */
    void UpdateVisionControls();

    /**
     * @brief ToggleFullScreen 切换普通窗口和全屏模式
     */
    void ToggleFullScreen();

    /**
     * @brief EnterFullScreen 进入全屏模式
     */
    void EnterFullScreen();

    /**
     * @brief ExitFullScreen 退出全屏模式
     */
    void ExitFullScreen();

    /**
     * @brief UpdateFullScreenButton 根据全屏状态更新按钮文本
     */
    void UpdateFullScreenButton();

    /**
     * @brief ShowCursor 显示鼠标
     */
    void ShowCursor();

    /**
     * @brief HideCursorIfFullScreen 全屏状态下隐藏鼠标
     */
    void HideCursorIfFullScreen();

    /**
     * @brief RestartCursorHideTimer 全屏状态下重启鼠标隐藏定时器
     */
    void RestartCursorHideTimer();

    /**
     * @brief HandleFullScreenMouseActivity 全屏状态下处理鼠标活动
     */
    void HandleFullScreenMouseActivity();

    /**
     * @brief UpdateMuteButton 更新静音按钮显示
     * @param muted 是否静音
     */
    void UpdateMuteButton(bool muted);

    /**
     * @brief PlayPrevious 切换到播放列表上一项并播放
     */
    void PlayPrevious();

    /**
     * @brief PlayNext 切换到播放列表下一项并播放
     */
    void PlayNext();

    /**
     * @brief HandlePlaybackFinished 根据播放模式处理当前媒体播放结束
     */
    void HandlePlaybackFinished();

    /**
     * @brief RefreshPlaylistView 刷新播放列表显示
     */
    void RefreshPlaylistView();

    /**
     * @brief RefreshStreamView 刷新多路流验证列表显示
     */
    void RefreshStreamView();

    /**
     * @brief UpdateStreamControls 根据多路流选择状态更新按钮
     */
    void UpdateStreamControls();

    /**
     * @brief AddMultiStreamFiles 添加多路文件源
     */
    void AddMultiStreamFiles();

    /**
     * @brief AddCameraStream 添加摄像头源（测试用）
     */
    void AddCameraStream();

    /**
     * @brief AddNetworkStream 添加 RTSP 网络流
     */
    void AddNetworkStream();

    /**
     * @brief RemoveSelectedStream 移除当前选中的流
     */
    void RemoveSelectedStream();

    /**
     * @brief SetSelectedActiveStream 将当前选中流设为活动显示流
     */
    void SetSelectedActiveStream();

    /**
     * @brief SetSelectedActiveAudioStream 将当前选中流设为活动音频流
     */
    void SetSelectedActiveAudioStream();

    /**
     * @brief SelectedStreamId 获取当前选中的流 ID
     * @return 当前选中流 ID，未选中时返回 InvalidStreamId
     */
    MediaPlayController::StreamId SelectedStreamId() const;

    /**
     * @brief SwitchToPlaylistIndex 切换到播放列表指定索引
     * @param index 播放列表索引
     * @param autoStart 切换成功后是否立即播放
     * @return true 切换成功
     * @return false 切换失败
     */
    bool SwitchToPlaylistIndex(int index, bool autoStart);

    bool isSliderDragging_ = false;	///< 是否正在拖动进度条
    bool isFullScreenMode_ = false;	///< 是否处于全屏模式
    bool isCursorHidden_ = false;	///< 鼠标是否已隐藏
    bool hasShownGrid4x4CapacityNotice_ = false;	///< 是否已提示 4x4 实验容量风险
    MultiViewLayout multiViewLayout_ = MultiViewLayout::Single;	///< 当前多画面布局
    MediaPlayController::StreamId focusedStreamId_ = MediaPlayController::InvalidStreamId;	///< 当前聚焦流 ID
    QMap<MediaPlayController::StreamId, QString> streamDisplayNames_;	///< 流显示名称表
    QMap<MediaPlayController::StreamId, int> streamSlotIndexes_;	///< 流到 slot 的绑定表

    /**
     * @brief formatTime 将秒数格式化为 UI 时间字符串
     * @param totalSeconds 总秒数
     * @return MM:SS 格式的时间字符串
     */
    QString formatTime(double totalSeconds);
};

#endif // MAINWINDOW_H

