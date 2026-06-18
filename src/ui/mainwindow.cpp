#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "domain/FileMediaSource.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QStringList>
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
    ui->listWidget_streams->setSelectionMode(QAbstractItemView::SingleSelection);
    setMouseTracking(true);
    ui->centralwidget->setMouseTracking(true);
    ui->widget_func->setMouseTracking(true);
    ui->listWidget_files->setMouseTracking(true);
    ui->listWidget_streams->setMouseTracking(true);
    ui->centralwidget->installEventFilter(this);
    ui->widget_func->installEventFilter(this);
    ui->listWidget_files->installEventFilter(this);
    ui->listWidget_streams->installEventFilter(this);
    InitializeVideoContainers();
    InitializePlayModeComboBox();
    InitializeVolumeControls();
    InitializePlaybackSpeedComboBox();
    InitializeCursorHideTimer();
    InitializeMultiViewLayout();

    CreateConnects();

    QString filepath = "D:\\githubProject\\TinyVideoProcessor\\resource\\动物发现自己被绿.mp4";
    playlistManager_->AddFile(filepath);
    if (!SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), false)) {
        qCritical() << "Controller 初始化失败";
        return;
    }

    UpdatePlaybackControls(mediaPlayController_->State());
    RefreshStreamView();
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

    connect(mediaPlayController_.get(), &MediaPlayController::PlaybackSpeedChanged,
            this, &MainWindow::UpdatePlaybackSpeedComboBox);

    connect(mediaPlayController_.get(), &MediaPlayController::PlaybackFinished,
            this, &MainWindow::HandlePlaybackFinished);

    connect(mediaPlayController_.get(), &MediaPlayController::VolumeChanged, this, [this](double volume) {
        const int sliderValue = static_cast<int>(volume * 100.0 + 0.5);
        QSignalBlocker blocker(ui->slider_volume);
        ui->slider_volume->setValue(sliderValue);
    });

    connect(mediaPlayController_.get(), &MediaPlayController::MuteChanged,
            this, &MainWindow::UpdateMuteButton);

    connect(mediaPlayController_.get(), &MediaPlayController::StreamAdded,
            this, [this](MediaPlayController::StreamId id) {
                if (!streamDisplayNames_.contains(id)) {
                    streamDisplayNames_.insert(id, QString("Stream %1").arg(id));
                }
                RefreshStreamView();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::StreamRemoved,
            this, [this](MediaPlayController::StreamId id) {
                streamDisplayNames_.remove(id);
                ReleaseVideoContainerForStream(id);
                RefreshStreamView();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ActiveStreamChanged,
            this, [this](MediaPlayController::StreamId) {
                RefreshStreamView();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ActiveAudioStreamChanged,
            this, [this](MediaPlayController::StreamId) {
                RefreshStreamView();
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

    connect(playlistManager_.get(), &PlaylistManager::PlayModeChanged, this, [this](PlaylistManager::PlayMode mode) {
        const int index = ui->comboBox_mode->findData(static_cast<int>(mode));
        if (index < 0 || ui->comboBox_mode->currentIndex() == index) {
            return;
        }

        QSignalBlocker blocker(ui->comboBox_mode);
        ui->comboBox_mode->setCurrentIndex(index);
    });

    connect(ui->listWidget_files, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const int index = ui->listWidget_files->row(item);
        SwitchToPlaylistIndex(index, true);
    });

    connect(ui->listWidget_streams, &QListWidget::currentRowChanged,
            this, [this](int) {
                UpdateStreamControls();
            });

    connect(ui->pushButton_prev, &QPushButton::clicked, this, &MainWindow::PlayPrevious);

    connect(ui->pushButton_next, &QPushButton::clicked, this, &MainWindow::PlayNext);

    connect(ui->comboBox_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        bool ok = false;
        const int modeValue = ui->comboBox_mode->itemData(index).toInt(&ok);
        if (!ok) {
            return;
        }

        playlistManager_->SetPlayMode(static_cast<PlaylistManager::PlayMode>(modeValue));
    });

    connect(ui->slider_volume, &QSlider::valueChanged, this, [this](int value) {
        mediaPlayController_->SetVolume(static_cast<double>(value) / 100.0);
    });

    connect(ui->pushButton_mute, &QPushButton::clicked, this, [this]() {
        mediaPlayController_->SetMuted(!mediaPlayController_->IsMuted());
    });

    connect(ui->comboBox_speed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        bool ok = false;
        const double speed = ui->comboBox_speed->itemData(index).toDouble(&ok);
        if (!ok) {
            return;
        }

        mediaPlayController_->SetPlaybackSpeed(speed);
    });

    connect(ui->comboBox_layout, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        bool ok = false;
        const int layoutValue = ui->comboBox_layout->itemData(index).toInt(&ok);
        if (!ok) {
            return;
        }

        SetMultiViewLayout(static_cast<MultiViewLayout>(layoutValue));
    });

    connect(ui->pushButton_fullscreen, &QPushButton::clicked,
            this, &MainWindow::ToggleFullScreen);

    connect(ui->pushButton_addStream, &QPushButton::clicked,
            this, &MainWindow::AddMultiStreamFiles);

    connect(ui->pushButton_removeStream, &QPushButton::clicked,
            this, &MainWindow::RemoveSelectedStream);

    connect(ui->pushButton_activeStream, &QPushButton::clicked,
            this, &MainWindow::SetSelectedActiveStream);

    connect(ui->pushButton_activeAudio, &QPushButton::clicked,
            this, &MainWindow::SetSelectedActiveAudioStream);

    connect(ui->pushButton_startAll, &QPushButton::clicked, this, [this]() {
        mediaPlayController_->StartAll();
    });

    connect(ui->pushButton_pauseAll, &QPushButton::clicked, this, [this]() {
        mediaPlayController_->PauseAll();
    });

    connect(ui->pushButton_stopAll, &QPushButton::clicked, this, [this]() {
        mediaPlayController_->StopAll();
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

    ShowCursor();
    if (cursorHideTimer_) {
        cursorHideTimer_->stop();
    }
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
    } else if (ev->key() == Qt::Key_F || ev->key() == Qt::Key_F11) {
        ToggleFullScreen();
        ev->accept();
    } else if (ev->key() == Qt::Key_Escape && isFullScreenMode_) {
        ExitFullScreen();
        ev->accept();
    } else if (ev->key() == Qt::Key_Up) {
        VolumeUp();
    } else if (ev->key() == Qt::Key_Down) {
        VolumeDown();
    } else if (ev->key() == Qt::Key_Left && ev->modifiers().testFlag(Qt::ControlModifier)) {
        PlayPrevious();
        ev->accept();
    } else if (ev->key() == Qt::Key_Right && ev->modifiers().testFlag(Qt::ControlModifier)) {
        PlayNext();
        ev->accept();
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

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    HandleFullScreenMouseActivity();
    QMainWindow::mouseMoveEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (HandleVideoContainerClick(watched, event)) {
        return true;
    }

    if (event->type() == QEvent::MouseMove) {
        HandleFullScreenMouseActivity();
    }

    return QMainWindow::eventFilter(watched, event);
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
    const bool hasPlaylistItem = playlistManager_ && playlistManager_->Count() > 0;
    ui->pushButton_play->setEnabled(!playing);
    ui->pushButton_pause->setEnabled(playing);
    ui->pushButton_prev->setEnabled(hasPlaylistItem);
    ui->pushButton_next->setEnabled(hasPlaylistItem);
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

void MainWindow::InitializePlayModeComboBox() {
    ui->comboBox_mode->clear();
    ui->comboBox_mode->addItem("顺序播放", static_cast<int>(PlaylistManager::PlayMode::Sequential));
    ui->comboBox_mode->addItem("单曲循环", static_cast<int>(PlaylistManager::PlayMode::SingleLoop));
    ui->comboBox_mode->addItem("列表循环", static_cast<int>(PlaylistManager::PlayMode::ListLoop));
    ui->comboBox_mode->addItem("随机播放", static_cast<int>(PlaylistManager::PlayMode::Random));

    const int currentModeIndex = ui->comboBox_mode->findData(
        static_cast<int>(playlistManager_->GetPlayMode()));
    if (currentModeIndex >= 0) {
        ui->comboBox_mode->setCurrentIndex(currentModeIndex);
    }
}

void MainWindow::InitializeVolumeControls() {
    ui->slider_volume->setRange(0, 100);
    ui->slider_volume->setValue(static_cast<int>(mediaPlayController_->Volume() * 100.0 + 0.5));
    UpdateMuteButton(mediaPlayController_->IsMuted());
}

void MainWindow::InitializePlaybackSpeedComboBox() {
    ui->comboBox_speed->clear();
    ui->comboBox_speed->addItem("0.5x", 0.5);
    ui->comboBox_speed->addItem("1.0x", 1.0);
    ui->comboBox_speed->addItem("1.5x", 1.5);
    ui->comboBox_speed->addItem("2.0x", 2.0);
    UpdatePlaybackSpeedComboBox(mediaPlayController_->PlaybackSpeed());
}

void MainWindow::InitializeCursorHideTimer() {
    cursorHideTimer_ = std::make_unique<QTimer>(this);
    cursorHideTimer_->setInterval(2000);
    cursorHideTimer_->setSingleShot(true);
    connect(cursorHideTimer_.get(), &QTimer::timeout,
            this, &MainWindow::HideCursorIfFullScreen);
}

void MainWindow::InitializeVideoContainers() {
    ui->widget_videoArea->setMouseTracking(true);
    ui->widget_videoArea->installEventFilter(this);
    InstallVideoContainerEventFilters();
}

void MainWindow::InstallVideoContainerEventFilters() {
    for (QWidget* container : VideoContainers()) {
        if (!container) {
            continue;
        }

        container->setMouseTracking(true);
        container->installEventFilter(this);
        const QList<QWidget*> childWidgets = container->findChildren<QWidget*>();
        for (QWidget* child : childWidgets) {
            child->installEventFilter(this);
        }
    }
}

void MainWindow::InitializeMultiViewLayout() {
    ui->comboBox_layout->clear();
    ui->comboBox_layout->addItem("Single", static_cast<int>(MultiViewLayout::Single));
    ui->comboBox_layout->addItem("2x2", static_cast<int>(MultiViewLayout::Grid2x2));
    UpdateLayoutComboBox(multiViewLayout_);
    RefreshMultiViewLayout();
}

void MainWindow::SetMultiViewLayout(MultiViewLayout layout) {
    if (multiViewLayout_ == layout) {
        return;
    }

    multiViewLayout_ = layout;
    UpdateLayoutComboBox(multiViewLayout_);
    RefreshMultiViewLayout();
    qInfo() << "Multi view layout changed, max visible streams:" << MaxVisibleStreams();
}

MainWindow::MultiViewLayout MainWindow::CurrentMultiViewLayout() const {
    return multiViewLayout_;
}

int MainWindow::MaxVisibleStreamsForLayout(MultiViewLayout layout) const {
    switch (layout) {
    case MultiViewLayout::Single:
        return 1;
    case MultiViewLayout::Grid2x2:
        return 4;
    }

    return 1;
}

int MainWindow::MaxVisibleStreams() const {
    return MaxVisibleStreamsForLayout(CurrentMultiViewLayout());
}

std::array<QWidget*, 4> MainWindow::VideoContainers() const {
    return {ui->video_widget, ui->video_widget_1, ui->video_widget_2, ui->video_widget_3};
}

QWidget* MainWindow::PrimaryVideoContainer() const {
    return ui->video_widget;
}

QWidget* MainWindow::VideoContainerAt(int index) const {
    const std::array<QWidget*, 4> containers = VideoContainers();
    if (index < 0 || index >= static_cast<int>(containers.size())) {
        return nullptr;
    }

    return containers.at(index);
}

int MainWindow::FirstAvailableVideoContainerIndex() const {
    const std::array<QWidget*, 4> containers = VideoContainers();
    for (int index = 0; index < static_cast<int>(containers.size()); ++index) {
        if (!containers.at(index)) {
            continue;
        }

        if (!streamContainerIndexes_.values().contains(index)) {
            return index;
        }
    }

    return -1;
}

void MainWindow::BindStreamToVideoContainer(MediaPlayController::StreamId streamId,
                                            int containerIndex) {
    if (streamId == MediaPlayController::InvalidStreamId || !VideoContainerAt(containerIndex)) {
        return;
    }

    streamContainerIndexes_.insert(streamId, containerIndex);
}

void MainWindow::ReleaseVideoContainerForStream(MediaPlayController::StreamId streamId) {
    streamContainerIndexes_.remove(streamId);
}

int MainWindow::ContainerIndexForStream(MediaPlayController::StreamId streamId) const {
    return streamContainerIndexes_.value(streamId, -1);
}

MediaPlayController::StreamId MainWindow::StreamIdForVideoContainerIndex(int containerIndex) const {
    for (auto it = streamContainerIndexes_.cbegin(); it != streamContainerIndexes_.cend(); ++it) {
        if (it.value() == containerIndex) {
            return it.key();
        }
    }

    return MediaPlayController::InvalidStreamId;
}

int MainWindow::VideoContainerIndexForWidget(QWidget* widget) const {
    if (!widget) {
        return -1;
    }

    const std::array<QWidget*, 4> containers = VideoContainers();
    for (int index = 0; index < static_cast<int>(containers.size()); ++index) {
        QWidget* container = containers.at(index);
        if (!container) {
            continue;
        }

        if (widget == container || container->isAncestorOf(widget)) {
            return index;
        }
    }

    return -1;
}

bool MainWindow::HandleVideoContainerClick(QObject* watched, QEvent* event) {
    if (!event || event->type() != QEvent::MouseButtonPress) {
        return false;
    }

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() != Qt::LeftButton) {
        return false;
    }

    QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
    const int containerIndex = VideoContainerIndexForWidget(sourceWidget);
    if (containerIndex < 0) {
        return false;
    }

    return ActivateStreamFromVideoContainer(containerIndex);
}

bool MainWindow::ActivateStreamFromVideoContainer(int containerIndex) {
    const MediaPlayController::StreamId streamId = StreamIdForVideoContainerIndex(containerIndex);
    if (streamId == MediaPlayController::InvalidStreamId) {
        return false;
    }

    if (!mediaPlayController_->SetActiveStream(streamId)) {
        qWarning() << "Set active stream from video container failed:" << streamId;
        return true;
    }

    const MediaPlayController::StreamId audioStreamId =
        mediaPlayController_->StreamHasAudio(streamId)
            ? streamId
            : MediaPlayController::InvalidStreamId;
    if (!mediaPlayController_->SetActiveAudioStream(audioStreamId)) {
        qWarning() << "Set active audio stream from video container failed:" << streamId;
    }

    RefreshStreamView();
    return true;
}

void MainWindow::RefreshMultiViewLayout() {
    const std::array<QWidget*, 4> containers = VideoContainers();
    int singleVisibleIndex = ContainerIndexForStream(mediaPlayController_->ActiveStream());
    if (singleVisibleIndex < 0) {
        singleVisibleIndex = 0;
    }

    for (int index = 0; index < static_cast<int>(containers.size()); ++index) {
        QWidget* container = containers.at(index);
        if (!container) {
            continue;
        }

        const bool visible = CurrentMultiViewLayout() == MultiViewLayout::Grid2x2
            || index == singleVisibleIndex;
        container->setVisible(visible);
    }
}

void MainWindow::UpdateLayoutComboBox(MultiViewLayout layout) {
    const int index = ui->comboBox_layout->findData(static_cast<int>(layout));
    if (index < 0 || ui->comboBox_layout->currentIndex() == index) {
        return;
    }

    QSignalBlocker blocker(ui->comboBox_layout);
    ui->comboBox_layout->setCurrentIndex(index);
}

void MainWindow::UpdatePlaybackSpeedComboBox(double speed) {
    const int index = ui->comboBox_speed->findData(speed);
    if (index < 0 || ui->comboBox_speed->currentIndex() == index) {
        return;
    }

    QSignalBlocker blocker(ui->comboBox_speed);
    ui->comboBox_speed->setCurrentIndex(index);
}

void MainWindow::ToggleFullScreen() {
    if (isFullScreenMode_) {
        ExitFullScreen();
    } else {
        EnterFullScreen();
    }
}

void MainWindow::EnterFullScreen() {
    if (isFullScreenMode_) {
        return;
    }

    isFullScreenMode_ = true;
    showFullScreen();
    UpdateFullScreenButton();
    ShowCursor();
    RestartCursorHideTimer();
}

void MainWindow::ExitFullScreen() {
    if (!isFullScreenMode_) {
        return;
    }

    isFullScreenMode_ = false;
    if (cursorHideTimer_) {
        cursorHideTimer_->stop();
    }
    ShowCursor();
    showNormal();
    UpdateFullScreenButton();
}

void MainWindow::UpdateFullScreenButton() {
    ui->pushButton_fullscreen->setText(isFullScreenMode_ ? "退出全屏" : "全屏");
}

void MainWindow::ShowCursor() {
    if (!isCursorHidden_) {
        return;
    }

    unsetCursor();
    isCursorHidden_ = false;
}

void MainWindow::HideCursorIfFullScreen() {
    if (!isFullScreenMode_ || isCursorHidden_) {
        return;
    }

    setCursor(Qt::BlankCursor);
    isCursorHidden_ = true;
}

void MainWindow::RestartCursorHideTimer() {
    if (!isFullScreenMode_ || !cursorHideTimer_) {
        return;
    }

    cursorHideTimer_->start();
}

void MainWindow::HandleFullScreenMouseActivity() {
    if (!isFullScreenMode_) {
        return;
    }

    ShowCursor();
    RestartCursorHideTimer();
}

void MainWindow::UpdateMuteButton(bool muted) {
    ui->pushButton_mute->setText(muted ? "取消静音" : "静音");
}

void MainWindow::PlayPrevious() {
    const int originalIndex = playlistManager_->CurrentIndex();
    if (!playlistManager_->MovePrevious()) {
        return;
    }

    if (!SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), true)
        && originalIndex >= 0
        && originalIndex < playlistManager_->Count()) {
        playlistManager_->SetCurrentIndex(originalIndex);
    }
}

void MainWindow::PlayNext() {
    const int originalIndex = playlistManager_->CurrentIndex();
    if (!playlistManager_->MoveNext()) {
        return;
    }

    if (!SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), true)
        && originalIndex >= 0
        && originalIndex < playlistManager_->Count()) {
        playlistManager_->SetCurrentIndex(originalIndex);
    }
}

void MainWindow::HandlePlaybackFinished() {
    if (playlistManager_->Count() == 0 || playlistManager_->CurrentIndex() < 0) {
        return;
    }

    switch (playlistManager_->GetPlayMode()) {
    case PlaylistManager::PlayMode::Sequential:
        return;
    case PlaylistManager::PlayMode::SingleLoop:
        SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), true);
        return;
    case PlaylistManager::PlayMode::ListLoop:
    case PlaylistManager::PlayMode::Random:
        PlayNext();
        return;
    }

    return;
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

    const bool hasPlaylistItem = playlistManager_->Count() > 0;
    ui->pushButton_prev->setEnabled(hasPlaylistItem);
    ui->pushButton_next->setEnabled(hasPlaylistItem);
}

void MainWindow::RefreshStreamView() {
    const MediaPlayController::StreamId selectedStreamId = SelectedStreamId();

    ui->listWidget_streams->clear();
    const QList<MediaPlayController::StreamId> streamIds = mediaPlayController_->StreamIds();
    for (MediaPlayController::StreamId id : streamIds) {
        const bool isActiveStream = id == mediaPlayController_->ActiveStream();
        const bool isActiveAudio = id == mediaPlayController_->ActiveAudioStream();
        QStringList marks;
        if (isActiveStream) {
            marks.append("View");
        }
        if (isActiveAudio) {
            marks.append("Audio");
        }

        const QString displayName = streamDisplayNames_.value(id, QString("Stream %1").arg(id));
        QString text = QString("#%1 %2").arg(id).arg(displayName);
        const int containerIndex = ContainerIndexForStream(id);
        if (containerIndex >= 0) {
            text += QString(" [Slot %1]").arg(containerIndex + 1);
        }
        if (!marks.isEmpty()) {
            text += QString(" [%1]").arg(marks.join(", "));
        }

        auto* item = new QListWidgetItem(text, ui->listWidget_streams);
        item->setData(Qt::UserRole, id);
        if (id == selectedStreamId) {
            ui->listWidget_streams->setCurrentItem(item);
        }
    }

    UpdateStreamControls();
    RefreshMultiViewLayout();
}

void MainWindow::UpdateStreamControls() {
    const bool hasStream = !mediaPlayController_->StreamIds().isEmpty();
    const bool hasSelectedStream = SelectedStreamId() != MediaPlayController::InvalidStreamId;
    const bool hasAvailableContainer = FirstAvailableVideoContainerIndex() >= 0;

    ui->pushButton_addStream->setEnabled(hasAvailableContainer);
    ui->pushButton_removeStream->setEnabled(hasSelectedStream);
    ui->pushButton_activeStream->setEnabled(hasSelectedStream);
    ui->pushButton_activeAudio->setEnabled(hasSelectedStream);
    ui->pushButton_startAll->setEnabled(hasStream);
    ui->pushButton_pauseAll->setEnabled(hasStream);
    ui->pushButton_stopAll->setEnabled(hasStream);
}

void MainWindow::AddMultiStreamFiles() {
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "选择多路视频文件",
        QString(),
        "Video Files (*.mp4 *.mkv *.avi *.mov *.flv);;All Files (*.*)");

    for (const QString& filePath : filePaths) {
        const int containerIndex = FirstAvailableVideoContainerIndex();
        QWidget* container = VideoContainerAt(containerIndex);
        if (!container) {
            qWarning() << "Add stream failed: no available video container";
            break;
        }

        std::shared_ptr<IMediaSource> mediaSource = std::make_shared<FileMediaSource>(filePath);
        const MediaPlayController::StreamId streamId =
            mediaPlayController_->AddStream(mediaSource, container);
        if (streamId == MediaPlayController::InvalidStreamId) {
            qWarning() << "Add stream failed:" << filePath;
            continue;
        }

        BindStreamToVideoContainer(streamId, containerIndex);
        streamDisplayNames_.insert(streamId, QFileInfo(filePath).fileName());
        InstallVideoContainerEventFilters();
    }

    RefreshStreamView();
}

void MainWindow::RemoveSelectedStream() {
    const MediaPlayController::StreamId streamId = SelectedStreamId();
    if (streamId == MediaPlayController::InvalidStreamId) {
        return;
    }

    if (!mediaPlayController_->RemoveStream(streamId)) {
        qWarning() << "Remove stream failed:" << streamId;
    }
}

void MainWindow::SetSelectedActiveStream() {
    const MediaPlayController::StreamId streamId = SelectedStreamId();
    if (streamId == MediaPlayController::InvalidStreamId) {
        return;
    }

    if (!mediaPlayController_->SetActiveStream(streamId)) {
        qWarning() << "Set active stream failed:" << streamId;
        return;
    }

    RefreshStreamView();
}

void MainWindow::SetSelectedActiveAudioStream() {
    const MediaPlayController::StreamId streamId = SelectedStreamId();
    if (streamId == MediaPlayController::InvalidStreamId) {
        return;
    }

    if (!mediaPlayController_->SetActiveAudioStream(streamId)) {
        qWarning() << "Set active audio stream failed:" << streamId;
        return;
    }

    RefreshStreamView();
}

MediaPlayController::StreamId MainWindow::SelectedStreamId() const {
    QListWidgetItem* selectedItem = ui->listWidget_streams->currentItem();
    if (!selectedItem) {
        return MediaPlayController::InvalidStreamId;
    }

    bool ok = false;
    const int streamId = selectedItem->data(Qt::UserRole).toInt(&ok);
    if (!ok) {
        return MediaPlayController::InvalidStreamId;
    }

    return streamId;
}

bool MainWindow::SwitchToPlaylistIndex(int index, bool autoStart) {
    const QList<PlaylistItem> items = playlistManager_->Items();
    if (index < 0 || index >= items.size()) {
        return false;
    }

    const int previousIndex = playlistManager_->CurrentIndex();
    const PlaylistItem item = items.at(index);
    std::shared_ptr<IMediaSource> mediaSource = std::make_shared<FileMediaSource>(item.filePath);
    if (!mediaPlayController_->SwitchSource(mediaSource, PrimaryVideoContainer(), autoStart)) {
        qWarning() << "Switch playlist item failed:" << item.filePath;
        if (previousIndex >= 0 && previousIndex < playlistManager_->Count()) {
            playlistManager_->SetCurrentIndex(previousIndex);
        }
        return false;
    }

    playlistManager_->SetCurrentIndex(index);
    const MediaPlayController::StreamId activeStreamId = mediaPlayController_->ActiveStream();
    if (activeStreamId != MediaPlayController::InvalidStreamId) {
        BindStreamToVideoContainer(activeStreamId, 0);
        streamDisplayNames_.insert(activeStreamId, item.displayName);
        InstallVideoContainerEventFilters();
        RefreshStreamView();
    }
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

