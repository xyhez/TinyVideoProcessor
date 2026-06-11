#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "domain/FileMediaSource.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mediaPlayController_(std::make_unique<MediaPlayController>())
{
    ui->setupUi(this);
    this->setWindowTitle("modernFFplay-V2.0");

    //1.应用层初始化
    QString filepath = "D:\\githubProject\\TinyVideoProcessor\\resource\\动物发现自己被绿.mp4";
    auto mediaSource = std::make_shared<FileMediaSource>(filepath);
    if (!mediaPlayController_->Initialize(mediaSource, ui->video_widget)) {
        qCritical() << "Controller 初始化失败";
        return;
    }

    connect(mediaPlayController_.get(), &MediaPlayController::StateChanged,
            this, &MainWindow::UpdatePlaybackControls);
    UpdatePlaybackControls(mediaPlayController_->State());

}

void MainWindow::NoticeWindow(QString message)
{
    QLabel *toast = new QLabel(this);
    toast->setText(message);
    toast->setStyleSheet(
        "QLabel {"
        "    background-color: rgba(0, 0, 0, 180);"
        "    color: white;"
        "    padding: 8px 16px;"
        "    border-radius: 4px;"
        "    font-size: 14px;"
        "}"
        );
    toast->setAlignment(Qt::AlignCenter);
    toast->adjustSize();

    int x = (this->width() - toast->width()) / 2;
    int y = this->height() / 3;
    toast->move(x, y);

    toast->show();

    QPropertyAnimation *animation = new QPropertyAnimation(toast, "windowOpacity");
    animation->setDuration(500);
    animation->setStartValue(0);
    animation->setEndValue(1);
    animation->start();

    QTimer::singleShot(2000, [toast, animation]() {
        QPropertyAnimation *fadeOut = new QPropertyAnimation(toast, "windowOpacity");
        fadeOut->setDuration(500);
        fadeOut->setStartValue(1);
        fadeOut->setEndValue(0);
        fadeOut->start();

        QObject::connect(fadeOut, &QPropertyAnimation::finished, [toast, animation]() {
            toast->deleteLater();
            animation->deleteLater();
        });
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);

    mediaPlayController_->Stop();

}

void MainWindow::keyReleaseEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Space) {
        if (mediaPlayController_->State() == MediaPlayController::PlaybackState::Playing) {
            Pause();
        } else {
            Start();
        }
        ev->accept();
    } else if (ev->key() == Qt::Key_Up) {
        VolumeUp();
    } else if (ev->key() == Qt::Key_Down) {
        VolumeDown();
    } else if (ev->key() == Qt::Key_Left) {
        Rewind();
    } else if (ev->key() == Qt::Key_Right) {
        Forward();
    }
    QMainWindow::keyReleaseEvent(ev);
}

MainWindow::~MainWindow()
{
    mediaPlayController_.reset();
    delete ui;
}

void MainWindow::Start() {
    mediaPlayController_->Start();
}

void MainWindow::Stop() {
    mediaPlayController_->Stop();
}

void MainWindow::Pause() {
    mediaPlayController_->Pause();
}

void MainWindow::UpdatePlaybackControls(MediaPlayController::PlaybackState state) {
    const bool playing = state == MediaPlayController::PlaybackState::Playing;
    ui->pushButton_play->setEnabled(!playing);
    ui->pushButton_pause->setEnabled(playing);
}

void MainWindow::VolumeUp() {
    mediaPlayController_->VolumeUp();

}

void MainWindow::VolumeDown() {

    mediaPlayController_->VolumeDown();
}

void MainWindow::Rewind() {
    mediaPlayController_->Rewind();
}

void MainWindow::Forward() {
    mediaPlayController_->Forward();
}

void MainWindow::on_pushButton_play_clicked()
{
    Start();
}

void MainWindow::on_pushButton_pause_clicked()
{
    Pause();
}
