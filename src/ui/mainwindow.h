#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QLabel>

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

    void noticeWindow(QString message);
    void closeEvent(QCloseEvent *event) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void start();
    void stop();
    void pause();
    void VolumeUp();
    void VolumeDown();

private slots:
    void on_pushButton_play_clicked();
    void on_pushButton_pause_clicked();

private:
    Ui::MainWindow *ui;

    std::unique_ptr<MediaPlayController> mediaPlayController_ = nullptr;

    bool pauseFlag_ = false;
};

#endif // MAINWINDOW_H
