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
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    // 链接信号槽
    void CreateConnects();

    void NoticeWindow(QString message);
    void closeEvent(QCloseEvent *event) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void Start();
    void Stop();
    void Pause();
    void VolumeUp();
    void VolumeDown();
    void Rewind();
    void Forward();

private slots:
    void on_pushButton_play_clicked();
    void on_pushButton_pause_clicked();

private:
    Ui::MainWindow *ui;

    std::unique_ptr<MediaPlayController> mediaPlayController_ = nullptr;

    void UpdatePlaybackControls(MediaPlayController::PlaybackState state);
};

#endif // MAINWINDOW_H
