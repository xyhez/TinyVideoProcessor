#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "domain/FileMediaSource.h"
#include "domain/CameraMediaSource.h"
#include "infrastructure/rendering/multiviewrenderer.h"
#include "infrastructure/rendering/singlestreamframesinkadapter.h"
#include "ui/CameraSettingsDialog.h"
#include "tiny_log/Logging.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mediaPlayController_(std::make_unique<MediaPlayController>())
    , playlistManager_(std::make_unique<PlaylistManager>())
{
    ui->setupUi(this);
    this->setWindowTitle("TinyVideoProcessor");
    ui->listWidget_files->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->listWidget_streams->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->listWidget_streams->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->splitter_workspace->setStretchFactor(0, 0);
    ui->splitter_workspace->setStretchFactor(1, 1);
    ui->splitter_workspace->setStretchFactor(2, 0);
    setMouseTracking(true);
    InitializeVideoArea();
    InitializePlayModeComboBox();
    InitializeVolumeControls();
    InitializePlaybackSpeedComboBox();
    InitializePreviewFrameRateControls();
    InitializeImageProcessingControls();
    InitializeVisionControls();
    InitializeCursorHideTimer();
    InitializeMultiViewRenderer();
    InitializeMultiViewLayout();

    ui->centralwidget->setMouseTracking(true);
    ui->centralwidget->installEventFilter(this);
    const auto childWidgets = ui->centralwidget->findChildren<QWidget*>();
    for (QWidget* widget : childWidgets) {
        widget->setMouseTracking(true);
        widget->installEventFilter(this);
    }

    CreateConnects();
    ui->statusbar->showMessage("正在初始化...");

    QString filepath = "../../resource/动物发现自己被绿.mp4";
    playlistManager_->AddFile(filepath);
    if (!SwitchToPlaylistIndex(playlistManager_->CurrentIndex(), false)) {
        LOG_CRITICAL_STREAM() << "Controller initialization failed";
        ui->statusbar->showMessage("初始化失败");
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
        if (mediaPlayController_->ActiveStream() != MediaPlayController::InvalidStreamId
            && mediaPlayController_->IsActiveStreamRealtime()) {
            ui->slider_progress->setRange(0, 0);
            ui->slider_progress->setEnabled(false);
            ui->label_duration->setText("LIVE");
            return;
        }

        ui->slider_progress->setRange(0,static_cast<int>(duration));
        ui->slider_progress->setEnabled(mediaPlayController_->IsActiveStreamSeekable()
                                        && mediaPlayController_->HasActiveStreamDuration()
                                        && static_cast<int>(duration) > 0);
        ui->label_duration->setText(mediaPlayController_->HasActiveStreamDuration()
                                    ? formatTime(duration)
                                    : formatTime(0.0));
    });

    connect(mediaPlayController_.get(), &MediaPlayController::PlaybackSpeedChanged,
            this, &MainWindow::UpdatePlaybackSpeedComboBox);

    connect(mediaPlayController_.get(), &MediaPlayController::PreviewFrameRateReductionChanged,
            this, [this](bool enabled) {
                QSignalBlocker originalBlocker(ui->radioButton_originalPreviewFrameRate);
                QSignalBlocker reduceBlocker(ui->radioButton_reducePreviewFrameRate);
                ui->radioButton_originalPreviewFrameRate->setChecked(!enabled);
                ui->radioButton_reducePreviewFrameRate->setChecked(enabled);
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ImageProcessingEnabledChanged,
            this, [this](bool enabled) {
                QSignalBlocker blocker(ui->checkBox_imageProcessingEnabled);
                ui->checkBox_imageProcessingEnabled->setChecked(enabled);
                UpdateImageProcessingControls();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ImageProcessingModeChanged,
            this, [this](MediaPlayController::ImageProcessingMode mode) {
                UpdateImageProcessingModeComboBox(mode);
                UpdateImageProcessingControls();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ImageProcessingThresholdChanged,
            this, [this](int threshold) {
                QSignalBlocker blocker(ui->spinBox_imageProcessingThreshold);
                ui->spinBox_imageProcessingThreshold->setValue(threshold);
            });

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
                if (multiViewRenderer_) {
                    multiViewRenderer_->DetachStream(id);
                }
                ReleaseVideoSlotForStream(id);
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

    connect(mediaPlayController_.get(), &MediaPlayController::StreamErrorOccurred,
            this, [this](MediaPlayController::StreamId id, const QString& message) {
                const QString streamName = id == MediaPlayController::InvalidStreamId
                    ? QString("媒体源")
                    : QString("Stream %1").arg(id);
                const QString errorMessage = message.isEmpty()
                    ? QString("未知错误")
                    : message;
                LOG_WARN_STREAM() << "Stream error:" << id << errorMessage;
                QMessageBox::warning(this, "媒体流错误", QString("%1：%2").arg(streamName, errorMessage));
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

    connect(ui->listWidget_streams, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                if (!item) {
                    return;
                }

                bool ok = false;
                const int streamId = item->data(Qt::UserRole).toInt(&ok);
                if (!ok) {
                    return;
                }

                ActivateStreamFromView(streamId);
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

    connect(ui->radioButton_reducePreviewFrameRate, &QRadioButton::toggled,
            this, [this](bool checked) {
                mediaPlayController_->SetPreviewFrameRateReductionEnabled(checked);
            });

    connect(ui->checkBox_imageProcessingEnabled, &QCheckBox::toggled,
            this, [this](bool checked) {
                mediaPlayController_->SetImageProcessingEnabled(checked);
                UpdateImageProcessingControls();
            });

    connect(ui->comboBox_imageProcessingMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                bool ok = false;
                const int modeValue = ui->comboBox_imageProcessingMode->itemData(index).toInt(&ok);
                if (!ok) {
                    return;
                }

                mediaPlayController_->SetImageProcessingMode(
                    static_cast<MediaPlayController::ImageProcessingMode>(modeValue));
                UpdateImageProcessingControls();
            });

    connect(ui->spinBox_imageProcessingThreshold,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            [this](int value) {
                mediaPlayController_->SetImageProcessingThreshold(value);
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ActiveVisionPipelineChanged,
            this, [this](const QString& id) {
                UpdateVisionPipelineComboBox(id);
                UpdateVisionControls();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::VisionPipelineStartupFailed,
            this, [this](const QString& id, const QString& error) {
                LOG_WARN_STREAM() << "Vision pipeline startup failed:" << id << error;
                const QString message = error.isEmpty()
                    ? QString("识别管线 %1 加载失败").arg(id)
                    : QString("识别管线 %1 加载失败：%2").arg(id, error);
                QMessageBox::warning(this, "视觉分析", message);
            });

    connect(mediaPlayController_.get(), &MediaPlayController::VisionOverlayEnabledChanged,
            this, [this](bool enabled) {
                QSignalBlocker blocker(ui->checkBox_visionOverlay);
                ui->checkBox_visionOverlay->setChecked(enabled);
            });

    connect(ui->comboBox_visionPipeline,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                const QString id = ui->comboBox_visionPipeline->itemData(index).toString();
                if (!mediaPlayController_->SetActiveVisionPipeline(id)) {
                    UpdateVisionPipelineComboBox(mediaPlayController_->ActiveVisionPipelineId());
                }
            });

    connect(ui->checkBox_visionOverlay, &QCheckBox::toggled,
            this, [this](bool checked) {
                mediaPlayController_->SetVisionOverlayEnabled(checked);
            });

    connect(ui->pushButton_fullscreen, &QPushButton::clicked,
            this, &MainWindow::ToggleFullScreen);

    connect(ui->pushButton_addStream, &QPushButton::clicked,
            this, &MainWindow::AddMultiStreamFiles);

    connect(ui->pushButton_addCamera, &QPushButton::clicked,
            this, &MainWindow::AddCameraStream);

    connect(ui->pushButton_addNetwork, &QPushButton::clicked,
            this, &MainWindow::AddNetworkStream);

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
        isSliderDragging_ = false;
        if (!mediaPlayController_->IsActiveStreamSeekable()) {
            return;
        }

        double target = static_cast<double>(ui->slider_progress->value());
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

    switch (state) {
    case MediaPlayController::PlaybackState::Playing:
        ui->statusbar->showMessage("播放中");
        break;
    case MediaPlayController::PlaybackState::Paused:
        ui->statusbar->showMessage("已暂停");
        break;
    case MediaPlayController::PlaybackState::Stopped:
        ui->statusbar->showMessage("就绪 · 已停止");
        break;
    }
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

void MainWindow::InitializePreviewFrameRateControls() {
    const bool enabled = mediaPlayController_->IsPreviewFrameRateReductionEnabled();
    ui->radioButton_originalPreviewFrameRate->setChecked(!enabled);
    ui->radioButton_reducePreviewFrameRate->setChecked(enabled);
}

void MainWindow::InitializeImageProcessingControls() {
    ui->comboBox_imageProcessingMode->clear();
    ui->comboBox_imageProcessingMode->addItem(
        "灰度",
        static_cast<int>(MediaPlayController::ImageProcessingMode::Grayscale));
    ui->comboBox_imageProcessingMode->addItem(
        "二值化",
        static_cast<int>(MediaPlayController::ImageProcessingMode::Threshold));
    ui->comboBox_imageProcessingMode->addItem(
        "边缘",
        static_cast<int>(MediaPlayController::ImageProcessingMode::EdgeDetection));

    ui->spinBox_imageProcessingThreshold->setRange(0, 255);
    ui->spinBox_imageProcessingThreshold->setValue(mediaPlayController_->ImageProcessingThreshold());
    ui->checkBox_imageProcessingEnabled->setChecked(mediaPlayController_->IsImageProcessingEnabled());
    UpdateImageProcessingModeComboBox(mediaPlayController_->CurrentImageProcessingMode());
    UpdateImageProcessingControls();
}

void MainWindow::InitializeVisionControls() {
    ui->comboBox_visionPipeline->clear();
    // 第一项为"关闭",data 为空字符串
    ui->comboBox_visionPipeline->addItem("关闭", QString());

    const QList<QString> availableIds = mediaPlayController_->AvailableVisionPipelineIds();
    for (const QString& id : availableIds) {
        ui->comboBox_visionPipeline->addItem(
            mediaPlayController_->VisionPipelineDisplayName(id),
            id);
    }

    UpdateVisionPipelineComboBox(mediaPlayController_->ActiveVisionPipelineId());

    ui->checkBox_visionOverlay->setChecked(mediaPlayController_->IsVisionOverlayEnabled());
    UpdateVisionControls();
}

void MainWindow::InitializeCursorHideTimer() {
    cursorHideTimer_ = std::make_unique<QTimer>(this);
    cursorHideTimer_->setInterval(2000);
    cursorHideTimer_->setSingleShot(true);
    connect(cursorHideTimer_.get(), &QTimer::timeout,
            this, &MainWindow::HideCursorIfFullScreen);
}

void MainWindow::InitializeVideoArea() {
    ui->widget_videoArea->setMouseTracking(true);
    ui->widget_videoArea->installEventFilter(this);
}

void MainWindow::InitializeMultiViewLayout() {
    ui->comboBox_layout->clear();
    ui->comboBox_layout->addItem("Single", static_cast<int>(MultiViewLayout::Single));
    ui->comboBox_layout->addItem("2x2", static_cast<int>(MultiViewLayout::Grid2x2));
    ui->comboBox_layout->addItem("3x3", static_cast<int>(MultiViewLayout::Grid3x3));
    ui->comboBox_layout->addItem("4x4", static_cast<int>(MultiViewLayout::Grid4x4));
    UpdateLayoutComboBox(multiViewLayout_);
    RefreshMultiViewLayout();
}

void MainWindow::InitializeMultiViewRenderer() {
    if (multiViewRenderer_) {
        return;
    }

    multiViewRenderer_ = new MultiViewRenderer(ui->widget_videoArea);
    multiViewRenderer_->setObjectName("multiViewRenderer");
    multiViewRenderer_->setMinimumSize(320, 240);
    ui->gridLayout_videoArea->addWidget(multiViewRenderer_, 0, 0, 2, 2);

    connect(multiViewRenderer_, &MultiViewRenderer::StreamClicked,
            this, &MainWindow::ActivateStreamFromView);
    connect(multiViewRenderer_, &MultiViewRenderer::StreamDoubleClicked,
            this, &MainWindow::ToggleFocusedStreamView);
}

void MainWindow::RefreshMultiViewRenderer() {
    if (!multiViewRenderer_) {
        return;
    }

    MultiViewRenderer::Layout rendererLayout = MultiViewRenderer::Layout::Single;
    if (!IsMultiViewFocused()) {
        switch (CurrentMultiViewLayout()) {
        case MultiViewLayout::Single:
            rendererLayout = MultiViewRenderer::Layout::Single;
            break;
        case MultiViewLayout::Grid2x2:
            rendererLayout = MultiViewRenderer::Layout::Grid2x2;
            break;
        case MultiViewLayout::Grid3x3:
            rendererLayout = MultiViewRenderer::Layout::Grid3x3;
            break;
        case MultiViewLayout::Grid4x4:
            rendererLayout = MultiViewRenderer::Layout::Grid4x4;
            break;
        }
    }

    multiViewRenderer_->SetLayout(rendererLayout);
    multiViewRenderer_->SetViewSlots(MultiViewRendererViewSlots());
    multiViewRenderer_->SetActiveStream(mediaPlayController_->ActiveStream());
}

MediaPlayController::FrameSinkFactory MainWindow::CreateMultiViewFrameSinkFactory() const {
    if (!multiViewRenderer_) {
        return {};
    }

    return [this](MediaPlayController::StreamId id) {
        if (!multiViewRenderer_) {
            return std::unique_ptr<IVideoFrameSink>();
        }

        return std::unique_ptr<IVideoFrameSink>(
            new SingleStreamFrameSinkAdapter(multiViewRenderer_, id));
    };
}

QMap<int, MediaPlayController::StreamId> MainWindow::MultiViewRendererViewSlots() const {
    QMap<int, MediaPlayController::StreamId> viewSlots;
    if (IsMultiViewFocused()) {
        viewSlots.insert(0, focusedStreamId_);
        return viewSlots;
    }

    for (auto it = streamSlotIndexes_.cbegin(); it != streamSlotIndexes_.cend(); ++it) {
        viewSlots.insert(it.value(), it.key());
    }

    return viewSlots;
}

bool MainWindow::IsMultiViewFocused() const {
    return focusedStreamId_ != MediaPlayController::InvalidStreamId &&
           streamSlotIndexes_.contains(focusedStreamId_);
}

void MainWindow::ToggleFocusedStreamView(MediaPlayController::StreamId streamId) {
    if (streamId == MediaPlayController::InvalidStreamId || !streamSlotIndexes_.contains(streamId)) {
        return;
    }

    if (focusedStreamId_ == streamId) {
        ExitFocusedStreamView();
        return;
    }

    focusedStreamId_ = streamId;
    ActivateStreamFromView(streamId);
    RefreshMultiViewLayout();
}

void MainWindow::ExitFocusedStreamView() {
    if (focusedStreamId_ == MediaPlayController::InvalidStreamId) {
        return;
    }

    focusedStreamId_ = MediaPlayController::InvalidStreamId;
    RefreshMultiViewLayout();
}

void MainWindow::SetMultiViewLayout(MultiViewLayout layout) {
    if (multiViewLayout_ == layout) {
        return;
    }

    focusedStreamId_ = MediaPlayController::InvalidStreamId;
    multiViewLayout_ = layout;
    UpdateLayoutComboBox(multiViewLayout_);
    RefreshMultiViewLayout();
    ShowGrid4x4CapacityNoticeIfNeeded();
    LOG_INFO_STREAM()
        << "Multi view layout changed, visible slots:" << MaxVisibleStreams()
        << "assignable slots:" << CurrentVideoSlotCapacity()
        << "max logical slots:" << MaxVideoSlots();
}

void MainWindow::ShowGrid4x4CapacityNoticeIfNeeded() {
    if (multiViewLayout_ != MultiViewLayout::Grid4x4 || hasShownGrid4x4CapacityNotice_) {
        return;
    }

    hasShownGrid4x4CapacityNotice_ = true;
    QMessageBox::information(
        this,
        "16 路实验模式",
        "4x4 布局允许添加最多 16 路媒体源，但 16 路真实播放受 CPU/GPU、视频分辨率、编码格式和硬解能力影响。"
        "建议优先使用低分辨率视频，并在压力较高时启用降帧预览。");
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
    case MultiViewLayout::Grid3x3:
        return 9;
    case MultiViewLayout::Grid4x4:
        return 16;
    }

    return 1;
}

int MainWindow::MaxVisibleStreams() const {
    return MaxVisibleStreamsForLayout(CurrentMultiViewLayout());
}

int MainWindow::MaxVideoSlots() const {
    return kMaxVideoSlotCount;
}

int MainWindow::CurrentVideoSlotCapacity() const {
    const int visibleSlots = MaxVisibleStreams();
    const int maxSlots = MaxVideoSlots();
    return visibleSlots < maxSlots ? visibleSlots : maxSlots;
}

QWidget* MainWindow::FallbackRendererContainer() const {
    return ui->widget_videoArea;
}

int MainWindow::FirstAvailableVideoSlotIndex() const {
    for (int index = 0; index < CurrentVideoSlotCapacity(); ++index) {
        if (!streamSlotIndexes_.values().contains(index)) {
            return index;
        }
    }

    return -1;
}

void MainWindow::BindStreamToVideoSlot(MediaPlayController::StreamId streamId,
                                       int slotIndex) {
    if (streamId == MediaPlayController::InvalidStreamId || !IsValidVideoSlotIndex(slotIndex)) {
        return;
    }

    for (auto it = streamSlotIndexes_.begin(); it != streamSlotIndexes_.end();) {
        if (it.key() != streamId && it.value() == slotIndex) {
            it = streamSlotIndexes_.erase(it);
        } else {
            ++it;
        }
    }

    streamSlotIndexes_.insert(streamId, slotIndex);
}

void MainWindow::ReleaseVideoSlotForStream(MediaPlayController::StreamId streamId) {
    if (focusedStreamId_ == streamId) {
        focusedStreamId_ = MediaPlayController::InvalidStreamId;
    }

    streamSlotIndexes_.remove(streamId);
}

int MainWindow::SlotIndexForStream(MediaPlayController::StreamId streamId) const {
    return streamSlotIndexes_.value(streamId, -1);
}

bool MainWindow::IsValidVideoSlotIndex(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < MaxVideoSlots();
}

bool MainWindow::ActivateStreamFromView(MediaPlayController::StreamId streamId) {
    if (streamId == MediaPlayController::InvalidStreamId) {
        return false;
    }

    if (!mediaPlayController_->SetActiveStream(streamId)) {
        LOG_WARN_STREAM() << "Set active stream from view failed:" << streamId;
        return true;
    }

    const MediaPlayController::StreamId audioStreamId =
        mediaPlayController_->StreamHasAudio(streamId)
            ? streamId
            : MediaPlayController::InvalidStreamId;
    if (!mediaPlayController_->SetActiveAudioStream(audioStreamId)) {
        LOG_WARN_STREAM() << "Set active audio stream from view failed:" << streamId;
    }

    RefreshStreamView();
    return true;
}

void MainWindow::RefreshMultiViewLayout() {
    RefreshMultiViewRenderer();
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

void MainWindow::UpdateImageProcessingModeComboBox(MediaPlayController::ImageProcessingMode mode) {
    const int index = ui->comboBox_imageProcessingMode->findData(static_cast<int>(mode));
    if (index < 0 || ui->comboBox_imageProcessingMode->currentIndex() == index) {
        return;
    }

    QSignalBlocker blocker(ui->comboBox_imageProcessingMode);
    ui->comboBox_imageProcessingMode->setCurrentIndex(index);
}

void MainWindow::UpdateImageProcessingControls() {
    const bool hasActiveStream =
        mediaPlayController_->ActiveStream() != MediaPlayController::InvalidStreamId;
    const bool processingEnabled = mediaPlayController_->IsImageProcessingEnabled();
    const bool thresholdMode =
        mediaPlayController_->CurrentImageProcessingMode()
        == MediaPlayController::ImageProcessingMode::Threshold;

    ui->checkBox_imageProcessingEnabled->setEnabled(hasActiveStream);
    ui->comboBox_imageProcessingMode->setEnabled(hasActiveStream);
    ui->label_imageProcessingThreshold->setEnabled(
        hasActiveStream && processingEnabled && thresholdMode);
    ui->spinBox_imageProcessingThreshold->setEnabled(
        hasActiveStream && processingEnabled && thresholdMode);
}

void MainWindow::UpdateVisionPipelineComboBox(const QString& activeId) {
    const int index = ui->comboBox_visionPipeline->findData(activeId);
    if (index < 0 || ui->comboBox_visionPipeline->currentIndex() == index) {
        return;
    }
    QSignalBlocker blocker(ui->comboBox_visionPipeline);
    ui->comboBox_visionPipeline->setCurrentIndex(index);
}

void MainWindow::UpdateVisionControls() {
    const bool hasActiveStream =
        mediaPlayController_->ActiveStream() != MediaPlayController::InvalidStreamId;
    ui->comboBox_visionPipeline->setEnabled(hasActiveStream);
    ui->checkBox_visionOverlay->setEnabled(hasActiveStream);
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
    const MediaPlayController::StreamId previousSelectedStreamId = SelectedStreamId();
    const QList<MediaPlayController::StreamId> streamIds = mediaPlayController_->StreamIds();
    MediaPlayController::StreamId selectedStreamId = MediaPlayController::InvalidStreamId;

    if (streamIds.contains(previousSelectedStreamId)) {
        selectedStreamId = previousSelectedStreamId;
    } else if (streamIds.contains(mediaPlayController_->ActiveStream())) {
        selectedStreamId = mediaPlayController_->ActiveStream();
    } else if (!streamIds.isEmpty()) {
        selectedStreamId = streamIds.first();
    }

    ui->listWidget_streams->clear();
    QListWidgetItem* selectedItem = nullptr;
    for (MediaPlayController::StreamId id : streamIds) {
        const bool isActiveStream = id == mediaPlayController_->ActiveStream();
        const bool isActiveAudio = id == mediaPlayController_->ActiveAudioStream();
        const bool hasAudio = mediaPlayController_->StreamHasAudio(id);
        QStringList marks;
        if (isActiveStream) {
            marks.append("View");
        }
        if (isActiveAudio) {
            marks.append("Audio");
        }

        const QString displayName = streamDisplayNames_.value(id, QString("Stream %1").arg(id));
        QString text = QString("#%1").arg(id);
        const int slotIndex = SlotIndexForStream(id);
        if (slotIndex >= 0) {
            text += QString(" [S%1]").arg(slotIndex + 1);
        }
        if (!marks.isEmpty()) {
            text += QString(" [%1]").arg(marks.join(", "));
        }
        text += QString(" %1").arg(displayName);

        auto* item = new QListWidgetItem(text, ui->listWidget_streams);
        item->setData(Qt::UserRole, id);
        item->setToolTip(QString("StreamId: %1\n名称: %2\nSlot: %3\n音频: %4\n显示: %5\n输出音频: %6")
            .arg(id)
            .arg(displayName)
            .arg(slotIndex >= 0 ? QString::number(slotIndex + 1) : QString("未绑定"))
            .arg(hasAudio ? QString("有") : QString("无"))
            .arg(isActiveStream ? QString("是") : QString("否"))
            .arg(isActiveAudio ? QString("是") : QString("否")));
        if (id == selectedStreamId) {
            selectedItem = item;
        }
    }

    if (selectedItem) {
        ui->listWidget_streams->setCurrentItem(selectedItem);
        ui->listWidget_streams->scrollToItem(selectedItem);
    }

    UpdateStreamControls();
    RefreshMultiViewLayout();
}

void MainWindow::UpdateStreamControls() {
    const bool hasStream = !mediaPlayController_->StreamIds().isEmpty();
    const MediaPlayController::StreamId selectedStreamId = SelectedStreamId();
    const bool hasSelectedStream = selectedStreamId != MediaPlayController::InvalidStreamId;
    const bool selectedStreamHasAudio =
        hasSelectedStream && mediaPlayController_->StreamHasAudio(selectedStreamId);
    const bool hasAvailableSlot = FirstAvailableVideoSlotIndex() >= 0;

    ui->pushButton_addStream->setEnabled(hasAvailableSlot);
    ui->pushButton_addCamera->setEnabled(hasAvailableSlot);
    ui->pushButton_addNetwork->setEnabled(hasAvailableSlot);
    ui->pushButton_removeStream->setEnabled(hasSelectedStream);
    ui->pushButton_activeStream->setEnabled(hasSelectedStream);
    ui->pushButton_activeAudio->setEnabled(selectedStreamHasAudio);
    ui->pushButton_activeAudio->setToolTip(
        selectedStreamHasAudio ? QString() : QString("选中的流没有音频，不能设为音频输出"));
    ui->pushButton_startAll->setEnabled(hasStream);
    ui->pushButton_pauseAll->setEnabled(hasStream);
    ui->pushButton_stopAll->setEnabled(hasStream);
    UpdateImageProcessingControls();
    UpdateVisionControls();
}

void MainWindow::AddMultiStreamFiles() {
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "选择多路视频文件",
        QString(),
        "Video Files (*.mp4 *.mkv *.avi *.mov *.flv);;All Files (*.*)");

    for (const QString& filePath : filePaths) {
        const int slotIndex = FirstAvailableVideoSlotIndex();
        if (slotIndex < 0) {
            LOG_WARN_STREAM() << "Add stream failed: no available video slot";
            break;
        }

        std::shared_ptr<IMediaSource> mediaSource = std::make_shared<FileMediaSource>(filePath);
        MediaPlayController::StreamId streamId = MediaPlayController::InvalidStreamId;
        const MediaPlayController::FrameSinkFactory frameSinkFactory =
            CreateMultiViewFrameSinkFactory();
        QWidget* fallbackRendererContainer = frameSinkFactory ? nullptr : FallbackRendererContainer();
        if (!frameSinkFactory && !fallbackRendererContainer) {
            LOG_WARN_STREAM() << "Add stream failed: fallback video container missing";
            break;
        }

        if (frameSinkFactory) {
            streamId = mediaPlayController_->AddStreamWithFrameSinkFactory(
                mediaSource,
                fallbackRendererContainer,
                frameSinkFactory);
        } else {
            streamId = mediaPlayController_->AddStream(mediaSource, fallbackRendererContainer);
        }
        if (streamId == MediaPlayController::InvalidStreamId) {
            LOG_WARN_STREAM() << "Add stream failed:" << filePath;
            continue;
        }

        BindStreamToVideoSlot(streamId, slotIndex);
        streamDisplayNames_.insert(streamId, QFileInfo(filePath).fileName());
    }

    RefreshStreamView();
}

void MainWindow::AddCameraStream() {
    // 打开摄像头设置对话框
    CameraSettingsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return; // 用户取消
    }

    // 获取用户选择的选项
    CameraOpenOptions options = dialog.GetSelectedOptions();
    if (!options.IsValid()) {
        LOG_WARN_STREAM() << "MainWindow::AddCameraStream() - Invalid camera options";
        return;
    }

    LOG_DEBUG_STREAM() << "MainWindow::AddCameraStream() - add camera:" << options.Description();

    // 查找可用的逻辑视频 slot，默认 VideoRender 回退时复用主视频区域容器。
    const int slotIndex = FirstAvailableVideoSlotIndex();
    if (slotIndex < 0) {
        LOG_WARN_STREAM() << "Add camera failed: no available video slot";
        return;
    }

    // 创建摄像头媒体源
    auto cameraSource = std::make_shared<CameraMediaSource>(options);

    MediaPlayController::StreamId streamId = MediaPlayController::InvalidStreamId;
    const MediaPlayController::FrameSinkFactory frameSinkFactory =
        CreateMultiViewFrameSinkFactory();
    QWidget* fallbackRendererContainer = frameSinkFactory ? nullptr : FallbackRendererContainer();
    if (!frameSinkFactory && !fallbackRendererContainer) {
        LOG_WARN_STREAM() << "Add camera failed: fallback video container missing";
        return;
    }

    if (frameSinkFactory) {
        streamId = mediaPlayController_->AddStreamWithFrameSinkFactory(
            cameraSource,
            fallbackRendererContainer,
            frameSinkFactory);
    } else {
        streamId = mediaPlayController_->AddStream(cameraSource, fallbackRendererContainer);
    }

    if (streamId == MediaPlayController::InvalidStreamId) {
        LOG_WARN_STREAM() << "Add camera failed: could not add stream";
        return;
    }

    BindStreamToVideoSlot(streamId, slotIndex);
    streamDisplayNames_.insert(streamId, QString("%1 (%2x%3@%4fps)")
        .arg(options.deviceName)
        .arg(options.width)
        .arg(options.height)
        .arg(options.frameRate));

    RefreshStreamView();

    LOG_DEBUG_STREAM() << "MainWindow::AddCameraStream() - camera added, StreamId:" << streamId;
}

void MainWindow::AddNetworkStream() {
    const int slotIndex = FirstAvailableVideoSlotIndex();
    if (slotIndex < 0) {
        LOG_WARN_STREAM() << "Add network stream failed: no available video slot";
        return;
    }

    bool accepted = false;
    const QString url = QInputDialog::getText(
        this,
        "添加 RTSP 网络流",
        "RTSP URL:",
        QLineEdit::Normal,
        "rtsp://",
        &accepted).trimmed();
    if (!accepted) {
        return;
    }

    if (url.isEmpty()) {
        QMessageBox::warning(this, "网络流错误", "RTSP URL 不能为空");
        return;
    }

    if (!url.startsWith("rtsp://", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "网络流错误", "当前阶段只支持 rtsp:// 网络流");
        return;
    }

    NetworkOpenOptions options;
    options.url = url;
    options.protocol = NetworkStreamProtocol::Rtsp;
    options.rtspTransport = "tcp";
    options.openTimeoutMs = 5000;
    options.readTimeoutMs = 5000;

    const MediaPlayController::FrameSinkFactory frameSinkFactory =
        CreateMultiViewFrameSinkFactory();
    QWidget* fallbackRendererContainer = frameSinkFactory ? nullptr : FallbackRendererContainer();
    if (!frameSinkFactory && !fallbackRendererContainer) {
        LOG_WARN_STREAM() << "Add network stream failed: fallback video container missing";
        return;
    }

    MediaPlayController::StreamId streamId = MediaPlayController::InvalidStreamId;
    if (frameSinkFactory) {
        streamId = mediaPlayController_->AddNetworkStreamWithFrameSinkFactory(
            options,
            fallbackRendererContainer,
            frameSinkFactory);
    } else {
        streamId = mediaPlayController_->AddNetworkStream(options, fallbackRendererContainer);
    }

    if (streamId == MediaPlayController::InvalidStreamId) {
        LOG_WARN_STREAM() << "Add network stream failed:" << url;
        return;
    }

    const QUrl parsedUrl(url);
    const QString displayName = parsedUrl.host().isEmpty()
        ? url
        : QString("RTSP %1").arg(parsedUrl.host());
    BindStreamToVideoSlot(streamId, slotIndex);
    streamDisplayNames_.insert(streamId, displayName);
    RefreshStreamView();
}

void MainWindow::RemoveSelectedStream() {
    const MediaPlayController::StreamId streamId = SelectedStreamId();
    if (streamId == MediaPlayController::InvalidStreamId) {
        return;
    }

    if (!mediaPlayController_->RemoveStream(streamId)) {
        LOG_WARN_STREAM() << "Remove stream failed:" << streamId;
    }
}

void MainWindow::SetSelectedActiveStream() {
    const MediaPlayController::StreamId streamId = SelectedStreamId();
    if (streamId == MediaPlayController::InvalidStreamId) {
        return;
    }

    if (!ActivateStreamFromView(streamId)) {
        LOG_WARN_STREAM() << "Set active stream failed:" << streamId;
    }
}

void MainWindow::SetSelectedActiveAudioStream() {
    const MediaPlayController::StreamId streamId = SelectedStreamId();
    if (streamId == MediaPlayController::InvalidStreamId) {
        return;
    }

    if (!mediaPlayController_->SetActiveAudioStream(streamId)) {
        LOG_WARN_STREAM() << "Set active audio stream failed:" << streamId;
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
    const MediaPlayController::FrameSinkFactory frameSinkFactory =
        CreateMultiViewFrameSinkFactory();
    QWidget* fallbackRendererContainer = frameSinkFactory ? nullptr : FallbackRendererContainer();
    const bool switched = frameSinkFactory
        ? mediaPlayController_->SwitchSourceWithFrameSinkFactory(
              mediaSource,
              fallbackRendererContainer,
              autoStart,
              frameSinkFactory)
        : mediaPlayController_->SwitchSource(mediaSource, fallbackRendererContainer, autoStart);
    if (!switched) {
        LOG_WARN_STREAM() << "Switch playlist item failed:" << item.filePath;
        if (previousIndex >= 0 && previousIndex < playlistManager_->Count()) {
            playlistManager_->SetCurrentIndex(previousIndex);
        }
        return false;
    }

    playlistManager_->SetCurrentIndex(index);
    const MediaPlayController::StreamId activeStreamId = mediaPlayController_->ActiveStream();
    if (activeStreamId != MediaPlayController::InvalidStreamId) {
        BindStreamToVideoSlot(activeStreamId, 0);
        streamDisplayNames_.insert(activeStreamId, item.displayName);
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


