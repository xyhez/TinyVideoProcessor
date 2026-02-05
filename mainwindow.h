#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QLabel>
#include <deque>  // 添加 deque 支持

#include "decoder.h"
#include "demuxer.h"
#include "audioplayer.h"
#include "videorender.h"
#include "syncmanager.h"
#include "global.h"

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
    void freeVec();
    void freePacketVec();
    void freeFrameVec();
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

    QString m_videoFileName = "F:/desk/图/VID_20260116_151539.mp4";

    std::shared_ptr<std::deque<AVPacket*>> m_APacket = nullptr;
    std::shared_ptr<std::deque<AVPacket*>> m_VPacket = nullptr;
    std::shared_ptr<std::deque<AVFrame*>> m_AFrame = nullptr;
    std::shared_ptr<std::deque<AVFrame*>> m_VFrame = nullptr;

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
