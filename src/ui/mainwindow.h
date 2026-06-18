#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include <array>
#include <memory>

#include "application/MediaPlayController.h"
#include "domain/PlaylistManager.h"

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
    enum class MultiViewLayout {
        Single,
        Grid2x2
    };

    Ui::MainWindow *ui;

    std::unique_ptr<MediaPlayController> mediaPlayController_ = nullptr;
    std::unique_ptr<PlaylistManager> playlistManager_ = nullptr;
    std::unique_ptr<QTimer> cursorHideTimer_ = nullptr;

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
     * @brief InitializeCursorHideTimer 初始化全屏鼠标隐藏定时器
     */
    void InitializeCursorHideTimer();

    /**
     * @brief InitializeVideoContainers 初始化多路视频容器状态
     */
    void InitializeVideoContainers();

    /**
     * @brief InstallVideoContainerEventFilters 为视频容器及其子控件安装事件过滤器
     */
    void InstallVideoContainerEventFilters();

    /**
     * @brief InitializeMultiViewLayout 初始化多路显示布局状态
     */
    void InitializeMultiViewLayout();

    /**
     * @brief SetMultiViewLayout 切换多路显示布局状态
     * @param layout 目标布局
     */
    void SetMultiViewLayout(MultiViewLayout layout);

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
     * @brief VideoContainers 获取当前 UI 中的多路视频容器
     * @return 视频容器指针数组
     */
    std::array<QWidget*, 4> VideoContainers() const;

    /**
     * @brief PrimaryVideoContainer 获取兼容当前单路播放的主视频容器
     * @return 主视频容器
     */
    QWidget* PrimaryVideoContainer() const;

    /**
     * @brief VideoContainerAt 获取指定索引的视频容器
     * @param index 容器索引
     * @return 视频容器，索引无效时返回 nullptr
     */
    QWidget* VideoContainerAt(int index) const;

    /**
     * @brief FirstAvailableVideoContainerIndex 获取第一个空闲视频容器索引
     * @return 空闲容器索引，无空闲容器时返回 -1
     */
    int FirstAvailableVideoContainerIndex() const;

    /**
     * @brief BindStreamToVideoContainer 绑定流和视频容器
     * @param streamId 流 ID
     * @param containerIndex 容器索引
     */
    void BindStreamToVideoContainer(MediaPlayController::StreamId streamId,
                                    int containerIndex);

    /**
     * @brief ReleaseVideoContainerForStream 释放流占用的视频容器
     * @param streamId 流 ID
     */
    void ReleaseVideoContainerForStream(MediaPlayController::StreamId streamId);

    /**
     * @brief ContainerIndexForStream 获取流绑定的视频容器索引
     * @param streamId 流 ID
     * @return 容器索引，未绑定时返回 -1
     */
    int ContainerIndexForStream(MediaPlayController::StreamId streamId) const;

    /**
     * @brief StreamIdForVideoContainerIndex 获取容器绑定的流 ID
     * @param containerIndex 容器索引
     * @return 流 ID，未绑定时返回 InvalidStreamId
     */
    MediaPlayController::StreamId StreamIdForVideoContainerIndex(int containerIndex) const;

    /**
     * @brief VideoContainerIndexForWidget 获取控件所属的视频容器索引
     * @param widget 事件来源控件
     * @return 容器索引，未命中时返回 -1
     */
    int VideoContainerIndexForWidget(QWidget* widget) const;

    /**
     * @brief HandleVideoContainerClick 处理视频容器点击事件
     * @param watched 事件来源
     * @param event 事件对象
     * @return true 点击已处理
     */
    bool HandleVideoContainerClick(QObject* watched, QEvent* event);

    /**
     * @brief ActivateStreamFromVideoContainer 根据视频容器切换活动流
     * @param containerIndex 容器索引
     * @return true 切换成功
     */
    bool ActivateStreamFromVideoContainer(int containerIndex);

    /**
     * @brief RefreshMultiViewLayout 根据当前布局刷新视频容器显示状态
     */
    void RefreshMultiViewLayout();

    /**
     * @brief UpdateVideoContainerStyles 根据 active stream 刷新视频容器视觉标记
     */
    void UpdateVideoContainerStyles();

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

    bool isSliderDragging_ = false;
    bool isFullScreenMode_ = false;
    bool isCursorHidden_ = false;
    MultiViewLayout multiViewLayout_ = MultiViewLayout::Single;
    QMap<MediaPlayController::StreamId, QString> streamDisplayNames_;
    QMap<MediaPlayController::StreamId, int> streamContainerIndexes_;

    /**
     * @brief formatTime 将秒数格式化为 UI 时间字符串
     * @param totalSeconds 总秒数
     * @return MM:SS 格式的时间字符串
     */
    QString formatTime(double totalSeconds);
};

#endif // MAINWINDOW_H
