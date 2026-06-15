#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QLabel>
#include <memory>

#include "application/MediaPlayController.h"

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
     * @brief NoticeWindow 显示短提示信息
     * @param message 提示内容
     */
    void NoticeWindow(QString message);

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

    /**
     * @brief UpdatePlaybackControls 根据播放状态更新按钮可用状态
     * @param state 当前播放状态
     */
    void UpdatePlaybackControls(MediaPlayController::PlaybackState state);

    bool isSliderDragging_ = false;

    /**
     * @brief formatTime 将秒数格式化为 UI 时间字符串
     * @param totalSeconds 总秒数
     * @return MM:SS 格式的时间字符串
     */
    QString formatTime(double totalSeconds);
};

#endif // MAINWINDOW_H
