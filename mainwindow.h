#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QLabel>

#include "decoder.h"
#include "demuxer.h"
#include "audioplayer.h"
#include "videorender.h"
#include "syncmanager.h"


QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    /**
     * @brief 创建需要连接的信号槽
     */
    void createConnect();

    /**
     * @brief noticeWindow 消息通知窗口
     */
    void noticeWindow(QString message);

    /**
     * @brief closeEvent 重写窗口关事件
     * @param event
     */
    void closeEvent(QCloseEvent *event) override;
    /**
     * @brief keyPressEvent
     * @param ev
     */
    // void keyPressEvent(QKeyEvent *ev)   override;

    /**
     * @brief keyReleaseEvent 重写键盘事件
     * @param ev
     */
    void keyReleaseEvent(QKeyEvent *ev) override;

    /**
     * @brief start 开始播放
     */
    void start();

    /**
     * @brief stop 停止播放
     */
    void stop();

    /**
     * @brief pause 暂停播放
     */
    void pause();

    /**
     * @brief VolumeUp 音量增大
     */
    void VolumeUp();

    /**
     * @brief VolumeDown 音量减小
     */
    void VolumeDown();

    /**
     * @brief freeVec 释放包队列和帧队列中剩余的指针
     */
    void freeVec();

    /**
     * @brief freePacketVec 释放包队列
     */
    void freePacketVec();

    /**
     * @brief freeFrameVec 释放帧队列
     */
    void freeFrameVec();

    /**
     * @brief freeRsources 释放线程资源和解复用资源
     */
    void freeRsources();
signals:
    /**
     * @brief e_stop 发送信号暂停线程和解码器和解复用器
     */
    void e_stop();

    /**
     * @brief e_Rewind 倒退信号
     */
    void e_Rewind();

    /**
     * @brief e_Forward 前进信号
     */
    void e_Forward();

    /**
     * @brief e_volumeUp 增大音量
     */
    void e_volumeUp();

    /**
     * @brief e_volumeDown 减小音量
     */
    void e_volumeDown();


private slots:
    void on_pushButton_play_clicked();

    void on_pushButton_pause_clicked();

private:
    Ui::MainWindow *ui;

    QString m_videoFileName = "F:/Qt6project/qffplay/动物发现自己被绿.mp4"; ///< 视频文件名

    std::shared_ptr<QVector<AVPacket*>> m_APacket = nullptr;        ///< 音频包队列
    std::shared_ptr<QVector<AVPacket*>> m_VPacket = nullptr;        ///< 视频包队列
    std::shared_ptr<QVector<AVFrame*>> m_AFrame = nullptr;          ///< 音频帧队列
    std::shared_ptr<QVector<AVFrame*>> m_VFrame = nullptr;          ///< 视频帧队列

    decoder* m_A_decoder = nullptr;         ///< 音频解码器
    decoder* m_V_decoder = nullptr;         ///< 视频解码器
    demuxer* m_demuxer = nullptr;           ///< 解复用器

    QThread* m_demuxerTH = nullptr;         ///< 解复用线程
    QThread* m_A_decoderTH = nullptr;       ///< 音频解码线程
    QThread* m_V_decoderTH = nullptr;       ///< 视频解码线程

    std::shared_ptr<syncmanager> m_syncManager = nullptr;          ///< 时钟管理器

    AudioPlayer* m_audioplay = nullptr;     ///< 音频帧播放器
    VideoRender* m_videoRender = nullptr;   ///< 视频帧渲染器

    QThread* m_audioplayTH = nullptr;       ///< 音频播放线程

    QMutex mutex;                           ///< 互斥锁

    bool pause_flag = false;                ///< 暂停状态监控

};
#endif // MAINWINDOW_H
