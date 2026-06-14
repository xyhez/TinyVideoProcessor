#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "domain/FileMediaSource.h"

#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mediaPlayController_(std::make_unique<MediaPlayController>())
{
    ui->setupUi(this);
    this->setWindowTitle("modernFFplay-V2.0");

    //1.应用层初始化
    QString filepath = "F:\\desk\\图\\VID_20260116_151539.mp4";
    std::shared_ptr<IMediaSource> mediaSource = std::make_shared<FileMediaSource>(filepath);
    if (!mediaPlayController_->Initialize(mediaSource, ui->video_widget)) {
        qCritical() << "Controller 初始化失败";
        return;
    }

    UpdatePlaybackControls(mediaPlayController_->State());

    CreateConnects();
}

void MainWindow::CreateConnects() {
    connect(mediaPlayController_.get(), &MediaPlayController::StateChanged,
            this, &MainWindow::UpdatePlaybackControls);

    connect(mediaPlayController_.get(),&MediaPlayController::PositionChanged,this,[this](double position) {
        if(!isSliderDragging_) {
            int pos = static_cast<int>(position);
            ui->slider_progress->setValue(pos);
            ui->label_position->setText(formatTime(position));
        }
    });

    connect(mediaPlayController_.get(),&MediaPlayController::DurationChanged,this,[this](double duration){
        ui->slider_progress->setRange(0,static_cast<int>(duration));
        ui->slider_progress->setEnabled(static_cast<int>(duration) > 0);
        ui->label_duration->setText(formatTime(duration));
    });

    connect(ui->slider_progress,&QSlider::sliderPressed,this,[this](){
        isSliderDragging_ = true;
    });

    connect(ui->slider_progress,&QSlider::sliderReleased,this,[this](){
        double target = static_cast<double>(ui->slider_progress->value());
        mediaPlayController_->SeekTo(target);
        isSliderDragging_ = false;
    });
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
    NoticeWindow("快退：10秒");
}

void MainWindow::Forward() {
    mediaPlayController_->Forward();
    NoticeWindow("快进：10秒");
}

void MainWindow::on_pushButton_play_clicked()
{
    Start();
}

void MainWindow::on_pushButton_pause_clicked()
{
    Pause();
}

QString MainWindow::formatTime(double totalSeconds) {
    int m = totalSeconds/60;
    int s = totalSeconds - m * 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

