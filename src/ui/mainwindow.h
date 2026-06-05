#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QLabel>

#include "infrastructure/decoder.h"
#include "infrastructure/demuxer.h"
#include "infrastructure/audioplayer.h"
#include "infrastructure/videorender.h"
#include "infrastructure/syncmanager.h"
#include "core/queue.h"
#include "domain/IMediaSource.h"
// #include "domain/FileMediaSource.h"

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

    void createConnect();
    void noticeWindow(QString message);
    void closeEvent(QCloseEvent *event) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void start();
    void stop();
    void pause();
    void VolumeUp();
    void VolumeDown();
    void freeRsources();

signals:
    void e_stop();
    void e_Rewind();
    void e_Forward();
    void e_volumeUp();
    void e_volumeDown();

private slots:
    void on_pushButton_play_clicked();
    void on_pushButton_pause_clicked();

private:
    Ui::MainWindow *ui;

    // QString m_videoFileName = "F:\\Qt6project\\modernFFplay\\resource\\动物发现自己被绿.mp4";

    ///< 队列（SyncQueue 自带 mutex/cv，自动释放队内元素）
    std::shared_ptr<SyncQueue<AVPacket*>> m_APacket;
    std::shared_ptr<SyncQueue<AVPacket*>> m_VPacket;
    std::shared_ptr<SyncQueue<AVFrame*>>  m_AFrame;
    std::shared_ptr<SyncQueue<AVFrame*>>  m_VFrame;

    std::shared_ptr<IMediaSource> MediaSource_ = nullptr;

    decoder* m_A_decoder = nullptr;
    decoder* m_V_decoder = nullptr;
    demuxer* m_demuxer = nullptr;

    QThread* m_demuxerTH = nullptr;
    QThread* m_A_decoderTH = nullptr;
    QThread* m_V_decoderTH = nullptr;

    std::shared_ptr<syncmanager> m_syncManager = nullptr;

    AudioPlayer* m_audioplay = nullptr;
    VideoRender* m_videoRender = nullptr;

    QThread* m_audioplayTH = nullptr;

    QMutex mutex;
    bool pause_flag = false;
};

#endif // MAINWINDOW_H
