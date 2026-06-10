#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QLabel>

#include "application/MediaPlayController.h"
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

    // MediaPlayController
    std::unique_ptr<MediaPlayController> mediaPlayController_ = nullptr;

    QMutex mutex;
};

#endif // MAINWINDOW_H
