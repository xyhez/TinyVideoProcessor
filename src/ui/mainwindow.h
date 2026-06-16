#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QLabel>
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
    Ui::MainWindow *ui;

    std::unique_ptr<MediaPlayController> mediaPlayController_ = nullptr;
    std::unique_ptr<PlaylistManager> playlistManager_ = nullptr;

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
     * @brief SwitchToPlaylistIndex 切换到播放列表指定索引
     * @param index 播放列表索引
     * @param autoStart 切换成功后是否立即播放
     * @return true 切换成功
     * @return false 切换失败
     */
    bool SwitchToPlaylistIndex(int index, bool autoStart);

    bool isSliderDragging_ = false;
    bool isFullScreenMode_ = false;

    /**
     * @brief formatTime 将秒数格式化为 UI 时间字符串
     * @param totalSeconds 总秒数
     * @return MM:SS 格式的时间字符串
     */
    QString formatTime(double totalSeconds);
};

#endif // MAINWINDOW_H
