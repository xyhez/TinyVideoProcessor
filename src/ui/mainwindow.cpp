#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "domain/FileMediaSource.h"

#include <QAbstractItemView>
#include <QDebug>
#include <QFileDialog>
#include <QListWidgetItem>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mediaPlayController_(std::make_unique<MediaPlayController>())
    , playlistManager_(std::make_unique<PlaylistManager>())
{
    ui->setupUi(this);
    this->setWindowTitle("modernFFplay-V2.0");
    ui->listWidget_files->setSelectionMode(QAbstractItemView::SingleSelection);

    CreateConnects();

    QString filepath = "D:\\githubProject\\TinyVideoProcessor\\resource\\动物发现自己被绿.mp4";
    playlistManager_->AddFile(filepath);
    if (!SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), false)) {
        qCritical() << "Controller 初始化失败";
        return;
    }

    UpdatePlaybackControls(mediaPlayController_->State());
}

void MainWindow::CreateConnects() {
    connect(mediaPlayController_.get(), &MediaPlayController::StateChanged,
            this, &MainWindow::UpdatePlaybackControls);

    connect(mediaPlayController_.get(),&MediaPlayController::PositionChanged,this,[this](double position) {
        if(!isSliderDragging_) {
            int pos = static_cast<int>(position);
            ui->slider_progress->setValue(pos);
            ui->label_current->setText(formatTime(position));
        }
    });

    connect(mediaPlayController_.get(),&MediaPlayController::DurationChanged,this,[this](double duration){
        ui->slider_progress->setRange(0,static_cast<int>(duration));
        ui->slider_progress->setEnabled(static_cast<int>(duration) > 0);
        ui->label_duration->setText(formatTime(duration));
    });

    connect(mediaPlayController_.get(),&MediaPlayController::PlaybackSpeedChanged,this,[](double speed){
        qDebug() << "Playback speed changed:" << speed;
    });

    connect(playlistManager_.get(), &PlaylistManager::PlaylistChanged, this, [this]() {
        RefreshPlaylistView();
    });

    connect(playlistManager_.get(), &PlaylistManager::CurrentIndexChanged, this, [this](int index) {
        if (index >= 0 && index < ui->listWidget_files->count()) {
            ui->listWidget_files->setCurrentRow(index);
        } else {
            ui->listWidget_files->clearSelection();
        }
    });

    connect(ui->listWidget_files, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const int index = ui->listWidget_files->row(item);
        SwitchToPlaylistIndex(index, true);
    });

    connect(ui->slider_progress,&QSlider::sliderPressed,this,[this](){
        isSliderDragging_ = true;
    });

    connect(ui->slider_progress,&QSlider::sliderMoved,this,[this](int value){
        ui->label_current->setText(formatTime(value));
    });

    connect(ui->slider_progress,&QSlider::sliderReleased,this,[this](){
        double target = static_cast<double>(ui->slider_progress->value());
        isSliderDragging_ = false;
        mediaPlayController_->SeekTo(target);
    });
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);

    mediaPlayController_->Shutdown();

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
    } else if (ev->key() == Qt::Key_1) {
        mediaPlayController_->SetPlaybackSpeed(0.5);
    } else if (ev->key() == Qt::Key_2) {
        mediaPlayController_->SetPlaybackSpeed(1.0);
    } else if (ev->key() == Qt::Key_3) {
        mediaPlayController_->SetPlaybackSpeed(1.5);
    } else if (ev->key() == Qt::Key_4) {
        mediaPlayController_->SetPlaybackSpeed(2.0);
    } else if (ev->key() == Qt::Key_O && ev->modifiers().testFlag(Qt::ControlModifier)) {
        OpenFile();
        ev->accept();
    } else if (ev->key() == Qt::Key_Delete && ev->modifiers().testFlag(Qt::ControlModifier)) {
        ClearPlaylist();
        ev->accept();
    } else if (ev->key() == Qt::Key_Delete) {
        RemoveSelectedPlaylistItem();
        ev->accept();
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

void MainWindow::OpenFile() {
    const int previousCount = playlistManager_->Count();
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "选择视频文件",
        QString(),
        "Video Files (*.mp4 *.mkv *.avi *.mov *.flv);;All Files (*.*)");

    if (filePaths.isEmpty()) {
        return;
    }

    playlistManager_->AddFiles(filePaths);
    if (previousCount == 0 && playlistManager_->Count() > 0) {
        SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), false);
    }
}

void MainWindow::RemoveSelectedPlaylistItem() {
    const int selectedIndex = ui->listWidget_files->currentRow();
    if (selectedIndex < 0) {
        return;
    }

    const bool removedCurrent = selectedIndex == playlistManager_->CurrentIndex();
    const bool wasPlaying = mediaPlayController_->State() == MediaPlayController::PlaybackState::Playing;

    playlistManager_->RemoveAt(selectedIndex);

    if (!removedCurrent) {
        return;
    }

    if (playlistManager_->CurrentItem().has_value()) {
        SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), wasPlaying);
    } else {
        mediaPlayController_->Shutdown();
    }
}

void MainWindow::ClearPlaylist() {
    if (playlistManager_->Count() == 0) {
        return;
    }

    playlistManager_->Clear();
    mediaPlayController_->Shutdown();
}

void MainWindow::RefreshPlaylistView() {
    ui->listWidget_files->clear();

    const QList<PlaylistItem> items = playlistManager_->Items();
    for (const PlaylistItem& item : items) {
        auto* listItem = new QListWidgetItem(item.displayName, ui->listWidget_files);
        listItem->setData(Qt::UserRole, item.filePath);
    }

    const int currentIndex = playlistManager_->CurrentIndex();
    if (currentIndex >= 0 && currentIndex < ui->listWidget_files->count()) {
        ui->listWidget_files->setCurrentRow(currentIndex);
    }
}

bool MainWindow::SwitchToPlaylistIndex(int index, bool autoStart) {
    const QList<PlaylistItem> items = playlistManager_->Items();
    if (index < 0 || index >= items.size()) {
        return false;
    }

    const int previousIndex = playlistManager_->CurrentIndex();
    const PlaylistItem item = items.at(index);
    std::shared_ptr<IMediaSource> mediaSource = std::make_shared<FileMediaSource>(item.filePath);
    if (!mediaPlayController_->SwitchSource(mediaSource, ui->video_widget, autoStart)) {
        qWarning() << "Switch playlist item failed:" << item.filePath;
        if (previousIndex >= 0 && previousIndex < playlistManager_->Count()) {
            playlistManager_->SetCurrentIndex(previousIndex);
        }
        return false;
    }

    playlistManager_->SetCurrentIndex(index);
    return true;
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

QString MainWindow::formatTime(double totalSeconds) {
    int m = totalSeconds/60;
    int s = totalSeconds - m * 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

