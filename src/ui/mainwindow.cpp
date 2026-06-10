#include "ui/mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mediaPlayController_(std::make_unique<MediaPlayController>())
{
    ui->setupUi(this);
    this->setWindowTitle("modernFFplay-V2.0");

    //1.应用层初始化
    QString filepath = "D:\\githubProject\\TinyVideoProcessor\\resource\\动物发现自己被绿.mp4";
    mediaPlayController_->Initialize(filepath,ui->video_widget);

}

void MainWindow::createConnect() {
    qDebug()<<"正在连接信号槽...";
}

void MainWindow::noticeWindow(QString message)
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

    m_APacket->SetStopFlag(true);
    m_VPacket->SetStopFlag(true);
    m_AFrame->SetStopFlag(true);
    m_VFrame->SetStopFlag(true);

    m_APacket->WakeAll();
    m_VPacket->WakeAll();
    m_AFrame->WakeAll();
    m_VFrame->WakeAll();

    m_demuxer->stop();
    m_A_decoder->stop();
    m_V_decoder->stop();
    m_audioplay->stop();
    m_videoRender->stopRendering();
}

void MainWindow::keyReleaseEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Space) {
        if(pause_flag == false) {
            pause();
            pause_flag = true;
        }else {
            start();
            pause_flag = false;
        }
        ev->accept();
    } else if(ev->key() == Qt::Key_Up) {
        m_audioplay->volumeUp();
    } else if(ev->key() == Qt::Key_Down) {
        m_audioplay->volumeDown();
    } else if(ev->key() == Qt::Key_Left) {
        emit e_Rewind();
    } else if(ev->key() == Qt::Key_Right) {
        pause();
        m_syncManager->Forword();
        emit e_Forward();
        start();
    }
    QMainWindow::keyReleaseEvent(ev);
}

MainWindow::~MainWindow()
{
    delete ui;
    freeRsources();
}

void MainWindow::start() {
    mediaPlayController_->Start();

    qDebug()<<"开始播放";
    m_demuxer->start();
    m_demuxerTH->start();

    m_A_decoder->start();
    m_A_decoderTH->start();
    m_V_decoder->start();
    m_V_decoderTH->start();

    m_audioplay->start();
    m_audioplayTH->start();

    m_videoRender->startRendering();
}

void MainWindow::stop() {
}

void MainWindow::pause() {
    m_demuxer->pause();
    m_A_decoder->pause();
    m_V_decoder->pause();
    m_audioplay->pause();
    m_videoRender->pause();
}

void MainWindow::VolumeUp() {
}

void MainWindow::VolumeDown() {
}

void MainWindow::freeRsources() {
    m_A_decoderTH->quit();
    m_A_decoderTH->wait();
    delete m_A_decoderTH;
    m_A_decoderTH = nullptr;
    m_V_decoderTH->quit();
    m_V_decoderTH->wait();
    delete m_V_decoderTH;
    m_V_decoderTH = nullptr;
    m_demuxerTH->quit();
    m_demuxerTH->wait();
    delete m_demuxerTH;
    m_demuxerTH = nullptr;
    m_audioplayTH->quit();
    m_audioplayTH->wait();
    delete m_audioplayTH;
    m_audioplayTH = nullptr;
    delete m_A_decoder;
    delete m_V_decoder;
    delete m_demuxer;
    delete m_audioplay;
}

void MainWindow::on_pushButton_play_clicked()
{
    start();
}

void MainWindow::on_pushButton_pause_clicked()
{
    pause();
}
