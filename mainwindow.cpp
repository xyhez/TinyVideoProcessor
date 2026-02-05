#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_APacket(std::make_shared<std::deque<AVPacket*>>())
    , m_VPacket(std::make_shared<std::deque<AVPacket*>>())
    , m_AFrame(std::make_shared<std::deque<AVFrame*>>())
    , m_VFrame(std::make_shared<std::deque<AVFrame*>>())
    , m_A_decoder(new decoder)
    , m_V_decoder(new decoder)
    , m_demuxer(new demuxer(m_APacket, m_VPacket, m_AFrame, m_VFrame))
    , m_demuxerTH(new QThread(this))
    , m_A_decoderTH(new QThread(this))
    , m_V_decoderTH(new QThread(this))
    , m_syncManager(std::make_shared<syncmanager>())
    , m_audioplay(new AudioPlayer)
    , m_audioplayTH(new QThread(this))
{
    ui->setupUi(this);
    this->setWindowTitle("modernFFplay-V1.0");

    m_demuxer->initDemuxer(m_videoFileName);
    m_demuxer->moveToThread(m_demuxerTH);

    m_A_decoder->initDecoder(m_APacket, m_AFrame,
                             m_demuxer->reAcoderPar(),
                             m_demuxer->reAtimebase());
    m_A_decoder->moveToThread(m_A_decoderTH);

    m_V_decoder->initDecoder(m_VPacket, m_VFrame,
                             m_demuxer->reVcoderPar(),
                             m_demuxer->reVtimebase());
    m_V_decoder->moveToThread(m_V_decoderTH);

    m_audioplay->initAudioPlay(m_demuxer->reAcoderPar(),
                               m_AFrame,
                               m_syncManager);
    m_audioplay->moveToThread(m_audioplayTH);

    m_videoRender = new VideoRender(ui->centralwidget);

    if (ui->openGLWidget) {
        ui->openGLWidget->setLayout(new QVBoxLayout());
        ui->openGLWidget->layout()->addWidget(m_videoRender);
    }
    m_videoRender->initvideoRender(m_VFrame,
                                   m_syncManager);

    createConnect();


    // 在 start() 中添加诊断定时器
    // QTimer *diagTimer = new QTimer(this);
    // connect(diagTimer, &QTimer::timeout, [this]() {
    //     qDebug() << "=== Queue Status ===";
    //     qDebug() << "VPacket:" << m_VPacket->size()
    //              << "VFrame:" << m_VFrame->size()
    //              << "APacket:" << m_APacket->size()
    //              << "AFrame:" << m_AFrame->size();
    // });
    // diagTimer->start(1000);
}

void MainWindow::createConnect() {
    qDebug()<<"正在连接信号槽...";

    connect(m_demuxerTH,&QThread::started,m_demuxer,&demuxer::startDemuxer);
    connect(m_A_decoderTH,&QThread::started,m_A_decoder,&decoder::startDecoder);
    connect(m_V_decoderTH,&QThread::started,m_V_decoder,&decoder::startDecoder);
    connect(m_audioplayTH,&QThread::started,m_audioplay,&AudioPlayer::startAudioPlay);
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
    g_stop = true;

    cv_A_packet_NotFull.notify_all();
    cv_A_packet_NotEmpty.notify_all();
    cv_A_frame_NotFull.notify_all();
    cv_A_frame_NotEmpty.notify_all();
    cv_V_packet_NotFull.notify_all();
    cv_V_packet_NotEmpty.notify_all();
    cv_V_frame_NotFull.notify_all();
    cv_V_frame_NotEmpty.notify_all();

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
    freeVec();
}

void MainWindow::start() {
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

void MainWindow::freeVec() {
    freePacketVec();
    freeFrameVec();
}

void MainWindow::freePacketVec() {
    mutex.lock();
    for(AVPacket* packet : *m_APacket) {
        if(packet)
            av_packet_free(&packet);
    }
    for(AVPacket* packet : *m_VPacket) {
        if(packet)
            av_packet_free(&packet);
    }
    mutex.unlock();
}

void MainWindow::freeFrameVec() {
    mutex.lock();
    if(!m_AFrame->empty()) {
        for(AVFrame* frame: *m_AFrame) {
            if(frame)
                av_frame_free(&frame);
        }
    }
    if(!m_VFrame->empty()) {
        for(AVFrame* frame: *m_VFrame) {
            if(frame)
                av_frame_free(&frame);
        }
    }
    mutex.unlock();
}

void MainWindow::on_pushButton_play_clicked()
{
    start();
}

void MainWindow::on_pushButton_pause_clicked()
{
    pause();
}
