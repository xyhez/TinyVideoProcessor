#include "ui/mainwindow.h"
#include "ui_mainwindow.h"
#include "domain/FileMediaSource.h"
#include "domain/CameraMediaSource.h"
#include "infrastructure/rendering/multiviewrenderer.h"
#include "infrastructure/rendering/singlestreamframesinkadapter.h"
#include "ui/CameraSettingsDialog.h"
#include "ui/StreamListItemDelegate.h"
#include "ui/StreamViewportOverlay.h"
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
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>

namespace {

QString StreamSourceKindDisplayName(StreamSourceKind sourceKind) {
    switch (sourceKind) {
    case StreamSourceKind::File:
        return QStringLiteral("本地文件");
    case StreamSourceKind::Camera:
        return QStringLiteral("摄像头");
    case StreamSourceKind::Rtsp:
        return QStringLiteral("RTSP");
    case StreamSourceKind::Unknown:
        return QStringLiteral("未知来源");
    }
    return QStringLiteral("未知来源");
}

QStringList StreamStatusLabels(const StreamPresentationState& state) {
    QStringList labels;
    if (!state.lastError.isEmpty()) {
        labels.append(QStringLiteral("最近错误"));
    }
    if (state.isActiveVideo) {
        labels.append(QStringLiteral("主画面"));
    }
    if (state.isActiveAudio) {
        labels.append(QStringLiteral("音频输出"));
    }
    if (state.isRealtime) {
        labels.append(QStringLiteral("LIVE"));
    }
    if (labels.isEmpty()) {
        labels.append(QStringLiteral("已加载"));
    }
    return labels;
}

QString StreamPresentationToolTip(const StreamPresentationState& state) {
    return QStringLiteral(
        "视频流 ID：%1\n名称：%2\n来源：%3\n画面槽位：%4\n状态：%5\n音频轨道：%6\n最近错误：%7")
        .arg(state.streamId)
        .arg(state.displayName)
        .arg(StreamSourceKindDisplayName(state.sourceKind))
        .arg(state.slotIndex >= 0
            ? QString::number(state.slotIndex + 1)
            : QStringLiteral("未绑定"))
        .arg(StreamStatusLabels(state).join(QStringLiteral("、")))
        .arg(state.hasAudio ? QStringLiteral("有") : QStringLiteral("无"))
        .arg(state.lastError.isEmpty() ? QStringLiteral("无") : state.lastError);
}

QString StreamPresentationAccessibleText(const StreamPresentationState& state) {
    return QStringLiteral("%1，%2，%3")
        .arg(state.displayName)
        .arg(StreamSourceKindDisplayName(state.sourceKind))
        .arg(StreamStatusLabels(state).join(QStringLiteral("，")));
}

QString CompactStatusName(const QString& name) {
    constexpr int kMaximumStatusNameLength = 18;
    if (name.size() <= kMaximumStatusNameLength) {
        return name;
    }
    return name.left(kMaximumStatusNameLength - 1) + QStringLiteral("…");
}

} // namespace

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
    ui->listWidget_streams->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->listWidget_streams->setUniformItemSizes(true);
    ui->listWidget_streams->setItemDelegate(
        new StreamListItemDelegate(ui->listWidget_streams));
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
    InitializeRuntimeMetrics();

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
        UpdatePlaybackControls(mediaPlayController_->State());
        RefreshStreamView();
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
            ui->slider_progress->setToolTip("实时视频源不支持拖动进度");
            ui->label_duration->setText("LIVE");
            return;
        }

        ui->slider_progress->setRange(0,static_cast<int>(duration));
        const bool canSeek = mediaPlayController_->IsActiveStreamSeekable()
            && mediaPlayController_->HasActiveStreamDuration()
            && static_cast<int>(duration) > 0;
        ui->slider_progress->setEnabled(canSeek);
        ui->slider_progress->setToolTip(
            canSeek ? "拖动定位当前视频" : "当前视频源暂不支持定位播放");
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
                RefreshRuntimeMetrics();
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
                if (!streamPresentationMetadata_.contains(id)) {
                    RegisterStreamPresentationMetadata(
                        id,
                        QString("视频源 %1").arg(id),
                        StreamSourceKind::Unknown,
                        false);
                }
                RefreshStreamView();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::StreamRemoved,
            this, [this](MediaPlayController::StreamId id) {
                streamPresentationMetadata_.remove(id);
                streamRuntimeMetrics_.remove(id);
                streamRuntimeMetricBaselines_.remove(id);
                if (multiViewRenderer_) {
                    multiViewRenderer_->DetachStream(id);
                }
                ReleaseVideoSlotForStream(id);
                RefreshStreamView();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ActiveStreamChanged,
            this, [this](MediaPlayController::StreamId) {
                RefreshRuntimeMetrics();
                RefreshStreamView();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::ActiveAudioStreamChanged,
            this, [this](MediaPlayController::StreamId) {
                RefreshStreamView();
            });

    connect(mediaPlayController_.get(), &MediaPlayController::StreamErrorOccurred,
            this, [this](MediaPlayController::StreamId id, const QString& message) {
                const QString errorMessage = message.isEmpty()
                    ? QString("未知错误")
                    : message;
                if (id != MediaPlayController::InvalidStreamId
                    && mediaPlayController_->StreamIds().contains(id)) {
                    StreamPresentationMetadata& metadata =
                        streamPresentationMetadata_[id];
                    if (metadata.displayName.isEmpty()) {
                        metadata.displayName = QString("视频源 %1").arg(id);
                    }
                    metadata.lastError = errorMessage;
                    RefreshStreamView();
                }
                const StreamPresentationMetadata metadata =
                    streamPresentationMetadata_.value(id);
                const QString streamName = id == MediaPlayController::InvalidStreamId
                    ? QString("媒体源")
                    : (metadata.displayName.isEmpty()
                        ? QString("视频源 %1").arg(id)
                        : metadata.displayName);
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
                RefreshRuntimeMetrics();
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
    StopRuntimeMetrics();
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
    StopRuntimeMetrics();
    if (multiViewRenderer_) {
        multiViewRenderer_->SetVisionResultStore(nullptr);
    }
    if (mediaPlayController_) {
        mediaPlayController_->Shutdown();
    }
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
    ui->pushButton_play->setToolTip(
        playing ? "当前正在播放" : "播放当前媒体或恢复已暂停视频");
    ui->pushButton_pause->setToolTip(
        playing ? "暂停当前播放" : "当前未播放");
    ui->pushButton_prev->setToolTip(
        hasPlaylistItem ? "播放列表上一项" : "播放列表为空");
    ui->pushButton_next->setToolTip(
        hasPlaylistItem ? "播放列表下一项" : "播放列表为空");

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
        "视频文件 (*.mp4 *.mkv *.avi *.mov *.flv);;所有文件 (*.*)");

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
    ui->slider_volume->setToolTip("调整音频输出音量");
    UpdateMuteButton(mediaPlayController_->IsMuted());
}

void MainWindow::InitializePlaybackSpeedComboBox() {
    ui->comboBox_speed->clear();
    ui->comboBox_speed->addItem("0.5x", 0.5);
    ui->comboBox_speed->addItem("1.0x", 1.0);
    ui->comboBox_speed->addItem("1.5x", 1.5);
    ui->comboBox_speed->addItem("2.0x", 2.0);
    ui->comboBox_speed->setToolTip("调整当前媒体的播放速度");
    UpdatePlaybackSpeedComboBox(mediaPlayController_->PlaybackSpeed());
}

void MainWindow::InitializePreviewFrameRateControls() {
    const bool enabled = mediaPlayController_->IsPreviewFrameRateReductionEnabled();
    ui->radioButton_originalPreviewFrameRate->setChecked(!enabled);
    ui->radioButton_reducePreviewFrameRate->setChecked(enabled);
    ui->radioButton_originalPreviewFrameRate->setToolTip("按视频源原始输出节奏预览");
    ui->radioButton_reducePreviewFrameRate->setToolTip(
        "降低预览刷新频率，减少渲染负载");
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

void MainWindow::InitializeRuntimeMetrics() {
    runtimeContextLabel_ = new QLabel(ui->statusbar);
    runtimeContextLabel_->setObjectName(QStringLiteral("label_runtimeContext"));
    runtimeContextLabel_->setMaximumWidth(520);
    ui->statusbar->addPermanentWidget(runtimeContextLabel_);

    ui->label_runtimeDetails->setToolTip(
        QStringLiteral("预览 FPS 表示提交给输出链的采样速率；调度丢帧不等于网络丢包。"));

    runtimeMetricsTimer_ = std::make_unique<QTimer>(this);
    runtimeMetricsTimer_->setInterval(kRuntimeMetricsIntervalMs);
    runtimeMetricsTimer_->setTimerType(Qt::CoarseTimer);
    connect(runtimeMetricsTimer_.get(), &QTimer::timeout,
            this, &MainWindow::RefreshRuntimeMetrics);

    runtimeMetricsElapsed_.start();
    UpdateRuntimeMetricLabels();
    runtimeMetricsTimer_->start();
}

void MainWindow::RefreshRuntimeMetrics() {
    if (!mediaPlayController_
        || !runtimeMetricsTimer_
        || !runtimeMetricsTimer_->isActive()) {
        return;
    }
    if (!runtimeMetricsElapsed_.isValid()) {
        runtimeMetricsElapsed_.start();
    }

    const qint64 capturedElapsedMs = runtimeMetricsElapsed_.elapsed();
    QMap<MediaPlayController::StreamId, StreamRuntimePresentationMetrics>
        nextPresentationMetrics;
    QMap<MediaPlayController::StreamId, StreamRuntimeMetricBaseline>
        nextBaselines;

    const QList<MediaPlayController::StreamRuntimeMetricsSnapshot> snapshots =
        mediaPlayController_->StreamRuntimeMetricsSnapshots();
    for (const MediaPlayController::StreamRuntimeMetricsSnapshot& snapshot : snapshots) {
        if (snapshot.streamId == MediaPlayController::InvalidStreamId) {
            continue;
        }

        StreamRuntimePresentationMetrics presentation;
        presentation.hasFrameSize = snapshot.frameWidth > 0 && snapshot.frameHeight > 0;
        presentation.frameWidth = snapshot.frameWidth;
        presentation.frameHeight = snapshot.frameHeight;
        presentation.videoCodecName = snapshot.videoCodecName;
        presentation.schedulerRunning = snapshot.schedulerRunning;
        presentation.imageProcessingEnabled = snapshot.imageProcessingEnabled;
        presentation.visionAnalysisActive = snapshot.visionAnalysisActive;

        const auto previousIt = streamRuntimeMetricBaselines_.constFind(snapshot.streamId);
        bool keepPreviousBaseline = false;
        if (previousIt != streamRuntimeMetricBaselines_.cend()
            && capturedElapsedMs >= previousIt->capturedElapsedMs
            && snapshot.submittedFrames >= previousIt->submittedFrames
            && snapshot.schedulerDroppedFrames >= previousIt->schedulerDroppedFrames) {
            const qint64 elapsedMs = capturedElapsedMs - previousIt->capturedElapsedMs;
            if (elapsedMs >= kMinimumRuntimeMetricsSampleMs) {
                const std::uint64_t submittedDelta =
                    snapshot.submittedFrames - previousIt->submittedFrames;
                const std::uint64_t droppedDelta =
                    snapshot.schedulerDroppedFrames - previousIt->schedulerDroppedFrames;

                presentation.hasPreviewFps = true;
                presentation.previewFps =
                    static_cast<double>(submittedDelta) * 1000.0
                    / static_cast<double>(elapsedMs);

                const std::uint64_t scheduledFrameDelta = submittedDelta + droppedDelta;
                if (scheduledFrameDelta > 0) {
                    presentation.hasSchedulerDropRate = true;
                    presentation.schedulerDropRate =
                        static_cast<double>(droppedDelta) * 100.0
                        / static_cast<double>(scheduledFrameDelta);
                }
            } else {
                const auto previousPresentationIt =
                    streamRuntimeMetrics_.constFind(snapshot.streamId);
                if (previousPresentationIt != streamRuntimeMetrics_.cend()) {
                    presentation.hasPreviewFps = previousPresentationIt->hasPreviewFps;
                    presentation.previewFps = previousPresentationIt->previewFps;
                    presentation.hasSchedulerDropRate =
                        previousPresentationIt->hasSchedulerDropRate;
                    presentation.schedulerDropRate =
                        previousPresentationIt->schedulerDropRate;
                }
                keepPreviousBaseline = true;
            }
        }

        nextPresentationMetrics.insert(snapshot.streamId, presentation);
        if (keepPreviousBaseline) {
            nextBaselines.insert(snapshot.streamId, previousIt.value());
        } else {
            StreamRuntimeMetricBaseline baseline;
            baseline.submittedFrames = snapshot.submittedFrames;
            baseline.schedulerDroppedFrames = snapshot.schedulerDroppedFrames;
            baseline.capturedElapsedMs = capturedElapsedMs;
            nextBaselines.insert(snapshot.streamId, baseline);
        }
    }

    streamRuntimeMetrics_.swap(nextPresentationMetrics);
    streamRuntimeMetricBaselines_.swap(nextBaselines);
    UpdateRuntimeMetricLabels();
    RefreshStreamViewportOverlay();
}

void MainWindow::UpdateRuntimeMetricLabels() {
    if (!mediaPlayController_) {
        return;
    }

    const QList<MediaPlayController::StreamId> streamIds =
        mediaPlayController_->StreamIds();
    const int totalStreamCount = static_cast<int>(streamIds.size());
    int sampledStreamCount = 0;
    int producingStreamCount = 0;
    double totalPreviewFps = 0.0;
    for (MediaPlayController::StreamId streamId : streamIds) {
        const auto metricsIt = streamRuntimeMetrics_.constFind(streamId);
        if (metricsIt == streamRuntimeMetrics_.cend()
            || !metricsIt->hasPreviewFps) {
            continue;
        }

        sampledStreamCount += 1;
        totalPreviewFps += metricsIt->previewFps;
        if (metricsIt->previewFps > 0.0) {
            producingStreamCount += 1;
        }
    }

    if (streamIds.isEmpty()) {
        ui->label_workspaceMode->setText(QStringLiteral("等待视频源 · 运行指标 --"));
    } else if (sampledStreamCount == 0) {
        ui->label_workspaceMode->setText(
            QStringLiteral("%1 路 · 运行指标采样中").arg(totalStreamCount));
    } else if (sampledStreamCount < totalStreamCount) {
        ui->label_workspaceMode->setText(
            QStringLiteral("%1 路 · 采样 %2/%1 · 预览 Σ %3 FPS")
                .arg(totalStreamCount)
                .arg(sampledStreamCount)
                .arg(totalPreviewFps, 0, 'f', 1));
    } else {
        ui->label_workspaceMode->setText(
            QStringLiteral("%1 路 · %2 路出帧 · 预览 Σ %3 FPS")
                .arg(totalStreamCount)
                .arg(producingStreamCount)
                .arg(totalPreviewFps, 0, 'f', 1));
    }

    const MediaPlayController::StreamId activeStreamId =
        mediaPlayController_->ActiveStream();
    const auto activeMetricsIt = streamRuntimeMetrics_.constFind(activeStreamId);
    const bool hasActiveMetrics = activeMetricsIt != streamRuntimeMetrics_.cend();
    const QString codecName = hasActiveMetrics
        && !activeMetricsIt->videoCodecName.isEmpty()
            ? activeMetricsIt->videoCodecName.toUpper()
            : QStringLiteral("--");
    const QString schedulerDropRate = hasActiveMetrics
        && activeMetricsIt->hasSchedulerDropRate
            ? QStringLiteral("%1%").arg(activeMetricsIt->schedulerDropRate, 0, 'f', 1)
            : QStringLiteral("--");
    const QString imageProcessingState = hasActiveMetrics
        ? (activeMetricsIt->imageProcessingEnabled
            ? QStringLiteral("已启用")
            : QStringLiteral("关闭"))
        : QStringLiteral("--");

    QString visionAnalysisState = QStringLiteral("--");
    if (hasActiveMetrics) {
        visionAnalysisState = QStringLiteral("关闭");
        if (activeMetricsIt->visionAnalysisActive) {
            const QString pipelineId = mediaPlayController_->ActiveVisionPipelineId();
            visionAnalysisState = pipelineId.isEmpty()
                ? QStringLiteral("已启用")
                : mediaPlayController_->VisionPipelineDisplayName(pipelineId);
        }
    }

    if (activeStreamId == MediaPlayController::InvalidStreamId) {
        ui->label_runtimeDetails->setText(
            streamIds.isEmpty()
                ? QStringLiteral("暂无视频源\n请从顶部添加文件、摄像头或 RTSP")
                : QStringLiteral("暂无活动视频\n请从左侧选择并设为主画面"));
    } else if (!hasActiveMetrics) {
        ui->label_runtimeDetails->setText(QStringLiteral("正在采样运行指标…"));
    } else {
        ui->label_runtimeDetails->setText(
            QStringLiteral(
                "编码格式　%1\n"
                "调度丢帧　%2\n"
                "图像处理　%3\n"
                "视觉分析　%4\n"
                "网络丢包　不可用\n"
                "推理吞吐　不可用")
                .arg(codecName)
                .arg(schedulerDropRate)
                .arg(imageProcessingState)
                .arg(visionAnalysisState));
    }

    if (!runtimeContextLabel_) {
        return;
    }

    const auto streamDisplayName = [this](MediaPlayController::StreamId streamId) {
        if (streamId == MediaPlayController::InvalidStreamId) {
            return QStringLiteral("无");
        }
        const StreamPresentationMetadata metadata =
            streamPresentationMetadata_.value(streamId);
        return metadata.displayName.isEmpty()
            ? QStringLiteral("视频源 %1").arg(streamId)
            : metadata.displayName;
    };

    const QString activeVideoName = streamDisplayName(activeStreamId);
    const QString activeAudioName =
        streamDisplayName(mediaPlayController_->ActiveAudioStream());
    const QString activeVisionPipelineId =
        mediaPlayController_->ActiveVisionPipelineId();
    const QString activeVisionName = activeVisionPipelineId.isEmpty()
        ? QStringLiteral("关闭")
        : mediaPlayController_->VisionPipelineDisplayName(activeVisionPipelineId);
    runtimeContextLabel_->setText(
        QStringLiteral("主：%1 · 音：%2 · 视觉：%3")
            .arg(CompactStatusName(activeVideoName))
            .arg(CompactStatusName(activeAudioName))
            .arg(CompactStatusName(activeVisionName)));
    runtimeContextLabel_->setToolTip(
        QStringLiteral("活动视频：%1\n活动音频：%2\n视觉管线：%3")
            .arg(activeVideoName)
            .arg(activeAudioName)
            .arg(activeVisionName));
}

void MainWindow::StopRuntimeMetrics() {
    if (runtimeMetricsTimer_) {
        runtimeMetricsTimer_->stop();
    }
    runtimeMetricsElapsed_.invalidate();
    streamRuntimeMetricBaselines_.clear();
    streamRuntimeMetrics_.clear();
}

void MainWindow::InitializeVideoArea() {
    ui->widget_videoArea->setMouseTracking(true);
    ui->widget_videoArea->installEventFilter(this);
}

void MainWindow::InitializeMultiViewLayout() {
    ui->comboBox_layout->clear();
    ui->comboBox_layout->addItem("单画面", static_cast<int>(MultiViewLayout::Single));
    ui->comboBox_layout->addItem("2×2", static_cast<int>(MultiViewLayout::Grid2x2));
    ui->comboBox_layout->addItem("3×3", static_cast<int>(MultiViewLayout::Grid3x3));
    ui->comboBox_layout->addItem("4×4", static_cast<int>(MultiViewLayout::Grid4x4));
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
    multiViewRenderer_->SetVisionResultStore(mediaPlayController_->VisionResultStoreInstance());
    ui->gridLayout_videoArea->addWidget(multiViewRenderer_, 0, 0, 2, 2);
    streamViewportOverlay_ = new StreamViewportOverlay(multiViewRenderer_);

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
    RefreshStreamViewportOverlay();
}

void MainWindow::RefreshStreamViewportOverlay() {
    if (!streamViewportOverlay_) {
        return;
    }

    QMap<int, StreamPresentationState> states;
    const QList<MediaPlayController::StreamId> streamIds =
        mediaPlayController_->StreamIds();
    for (MediaPlayController::StreamId streamId : streamIds) {
        states.insert(streamId, BuildStreamPresentationState(streamId));
    }
    streamViewportOverlay_->SetPresentationStates(states);
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
    UpdateStreamControls();
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
        "4×4 布局允许添加最多 16 路媒体源，但 16 路真实播放受 CPU/GPU、视频分辨率、编码格式和硬解能力影响。"
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

    ui->checkBox_imageProcessingEnabled->setToolTip(
        hasActiveStream
            ? "为活动视频启用或关闭图像处理"
            : "请先选择活动视频");
    ui->comboBox_imageProcessingMode->setToolTip(
        hasActiveStream
            ? "选择活动视频的图像处理模式"
            : "请先选择活动视频");
    ui->label_imageProcessingMode->setToolTip(
        ui->comboBox_imageProcessingMode->toolTip());

    QString thresholdToolTip;
    if (!hasActiveStream) {
        thresholdToolTip = "请先选择活动视频";
    } else if (!processingEnabled) {
        thresholdToolTip = "启用图像处理后可调整阈值";
    } else if (!thresholdMode) {
        thresholdToolTip = "仅二值化模式可调整阈值";
    } else {
        thresholdToolTip = "设置二值化阈值（0～255）";
    }
    ui->label_imageProcessingThreshold->setToolTip(thresholdToolTip);
    ui->spinBox_imageProcessingThreshold->setToolTip(thresholdToolTip);
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
    ui->comboBox_visionPipeline->setToolTip(
        hasActiveStream
            ? "选择活动视频的视觉分析流水线"
            : "请先选择活动视频");
    ui->label_visionPipeline->setToolTip(ui->comboBox_visionPipeline->toolTip());
    ui->checkBox_visionOverlay->setToolTip(
        hasActiveStream
            ? "显示或隐藏活动视频的识别叠加层"
            : "请先选择活动视频");
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
    ui->pushButton_fullscreen->setToolTip(
        isFullScreenMode_ ? "退出全屏工作区" : "进入全屏工作区");
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
    ui->pushButton_mute->setToolTip(
        muted ? "恢复当前音频输出" : "静音当前音频输出");
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
    ui->pushButton_prev->setToolTip(
        hasPlaylistItem ? "播放列表上一项" : "播放列表为空");
    ui->pushButton_next->setToolTip(
        hasPlaylistItem ? "播放列表下一项" : "播放列表为空");
    ui->label_mediaHint->setText(
        hasPlaylistItem
            ? "Ctrl+O 添加媒体 · 双击列表播放"
            : "暂无媒体 · Ctrl+O 添加媒体");
    ui->listWidget_files->setToolTip(
        hasPlaylistItem
            ? "双击列表项开始播放"
            : "暂无媒体，请按 Ctrl+O 添加视频文件");
}

void MainWindow::RegisterStreamPresentationMetadata(
    MediaPlayController::StreamId id,
    const QString& displayName,
    StreamSourceKind sourceKind,
    bool isRealtime) {
    if (id == MediaPlayController::InvalidStreamId) {
        return;
    }

    StreamPresentationMetadata& metadata = streamPresentationMetadata_[id];
    metadata.displayName = displayName.isEmpty()
        ? QString("视频源 %1").arg(id)
        : displayName;
    metadata.sourceKind = sourceKind;
    metadata.isRealtime = isRealtime;
}

StreamPresentationState MainWindow::BuildStreamPresentationState(
    MediaPlayController::StreamId id) const {
    const StreamPresentationMetadata metadata =
        streamPresentationMetadata_.value(id);

    StreamPresentationState state;
    state.streamId = id;
    state.displayName = metadata.displayName.isEmpty()
        ? QString("视频源 %1").arg(id)
        : metadata.displayName;
    state.sourceKind = metadata.sourceKind;
    state.slotIndex = SlotIndexForStream(id);
    state.hasAudio = mediaPlayController_->StreamHasAudio(id);
    state.isActiveVideo = id == mediaPlayController_->ActiveStream();
    state.isActiveAudio = id == mediaPlayController_->ActiveAudioStream();
    state.isRealtime = metadata.isRealtime;
    state.lastError = metadata.lastError;
    state.runtimeMetrics = streamRuntimeMetrics_.value(id);
    return state;
}

void MainWindow::RefreshStreamView() {
    const MediaPlayController::StreamId previousSelectedStreamId = SelectedStreamId();
    const QList<MediaPlayController::StreamId> streamIds = mediaPlayController_->StreamIds();
    const bool hasStream = !streamIds.isEmpty();
    MediaPlayController::StreamId selectedStreamId = MediaPlayController::InvalidStreamId;

    ui->label_streams->setText(
        hasStream
            ? QString("视频源 · %1 路").arg(static_cast<int>(streamIds.size()))
            : QString("视频源 · 暂无"));
    ui->label_viewportHint->setText(
        hasStream
            ? "单击激活 · 双击聚焦"
            : "暂无画面 · 请先添加视频源");
    ui->listWidget_streams->setToolTip(
        hasStream
            ? "单击选择视频源，双击设为主画面"
            : "暂无视频源，请从顶部添加文件、摄像头或 RTSP");

    if (streamIds.contains(previousSelectedStreamId)) {
        selectedStreamId = previousSelectedStreamId;
    } else if (streamIds.contains(mediaPlayController_->ActiveStream())) {
        selectedStreamId = mediaPlayController_->ActiveStream();
    } else if (!streamIds.isEmpty()) {
        selectedStreamId = streamIds.first();
    }

    QMap<MediaPlayController::StreamId, QListWidgetItem*> existingItems;
    for (int row = 0; row < ui->listWidget_streams->count(); ++row) {
        QListWidgetItem* item = ui->listWidget_streams->item(row);
        bool ok = false;
        const MediaPlayController::StreamId id =
            item->data(Qt::UserRole).toInt(&ok);
        if (ok) {
            existingItems.insert(id, item);
        }
    }

    for (int row = ui->listWidget_streams->count() - 1; row >= 0; --row) {
        QListWidgetItem* item = ui->listWidget_streams->item(row);
        bool ok = false;
        const MediaPlayController::StreamId id =
            item->data(Qt::UserRole).toInt(&ok);
        if (!ok || !streamIds.contains(id)) {
            if (ok) {
                existingItems.remove(id);
            }
            delete ui->listWidget_streams->takeItem(row);
        }
    }

    QListWidgetItem* selectedItem = nullptr;
    for (MediaPlayController::StreamId id : streamIds) {
        QListWidgetItem* item = existingItems.value(id, nullptr);
        if (!item) {
            item = new QListWidgetItem(ui->listWidget_streams);
        }

        const StreamPresentationState state = BuildStreamPresentationState(id);
        item->setText(state.displayName);
        item->setData(Qt::UserRole, id);
        item->setData(
            kStreamPresentationStateRole,
            QVariant::fromValue(state));
        item->setData(
            Qt::AccessibleTextRole,
            StreamPresentationAccessibleText(state));
        item->setToolTip(StreamPresentationToolTip(state));
        if (id == selectedStreamId) {
            selectedItem = item;
        }
    }

    if (selectedItem && ui->listWidget_streams->currentItem() != selectedItem) {
        ui->listWidget_streams->setCurrentItem(selectedItem);
        ui->listWidget_streams->scrollToItem(selectedItem);
    } else if (!selectedItem) {
        ui->listWidget_streams->clearSelection();
    }

    UpdateStreamControls();
    UpdateRuntimeMetricLabels();
    RefreshMultiViewLayout();
}

void MainWindow::UpdateStreamControls() {
    const bool hasStream = !mediaPlayController_->StreamIds().isEmpty();
    const bool hasActiveStream =
        mediaPlayController_->ActiveStream() != MediaPlayController::InvalidStreamId;
    const MediaPlayController::StreamId selectedStreamId = SelectedStreamId();
    const bool hasSelectedStream = selectedStreamId != MediaPlayController::InvalidStreamId;
    const bool selectedStreamHasAudio =
        hasSelectedStream && mediaPlayController_->StreamHasAudio(selectedStreamId);
    const bool hasAvailableSlot = FirstAvailableVideoSlotIndex() >= 0;
    const bool allLogicalSlotsUsed =
        static_cast<int>(mediaPlayController_->StreamIds().size()) >= MaxVideoSlots();

    ui->pushButton_addStream->setEnabled(hasAvailableSlot);
    ui->pushButton_addCamera->setEnabled(hasAvailableSlot);
    ui->pushButton_addNetwork->setEnabled(hasAvailableSlot);
    const QString unavailableSlotToolTip =
        allLogicalSlotsUsed
            ? "视频槽位已满，请先移除视频源"
            : "当前布局画面已满，请切换布局或移除视频源";
    ui->pushButton_addStream->setToolTip(
        hasAvailableSlot
            ? "向多画面工作区添加一个或多个本地文件"
            : unavailableSlotToolTip);
    ui->pushButton_addCamera->setToolTip(
        hasAvailableSlot
            ? "枚举并添加本机摄像头"
            : unavailableSlotToolTip);
    ui->pushButton_addNetwork->setToolTip(
        hasAvailableSlot
            ? "添加 rtsp:// 网络视频源"
            : unavailableSlotToolTip);
    ui->pushButton_removeStream->setEnabled(hasSelectedStream);
    ui->pushButton_activeStream->setEnabled(hasSelectedStream);
    ui->pushButton_activeAudio->setEnabled(selectedStreamHasAudio);
    ui->pushButton_removeStream->setToolTip(
        hasSelectedStream ? "移除选中的视频源" : "请先选择视频源");
    ui->pushButton_activeStream->setToolTip(
        hasSelectedStream ? "将选中视频源设为主画面" : "请先选择视频源");
    if (!hasSelectedStream) {
        ui->pushButton_activeAudio->setToolTip("请先选择视频源");
    } else if (!selectedStreamHasAudio) {
        ui->pushButton_activeAudio->setToolTip(
            "选中的视频源没有音频轨道，不能设为音频输出");
    } else {
        ui->pushButton_activeAudio->setToolTip(
            "将选中视频源设为音频输出");
    }
    ui->pushButton_startAll->setEnabled(hasStream);
    ui->pushButton_pauseAll->setEnabled(hasStream);
    ui->pushButton_stopAll->setEnabled(hasStream);
    ui->pushButton_startAll->setToolTip(
        hasStream ? "启动全部视频源" : "请先添加视频源");
    ui->pushButton_pauseAll->setToolTip(
        hasStream ? "暂停全部视频源" : "请先添加视频源");
    ui->pushButton_stopAll->setToolTip(
        hasStream ? "停止全部视频源" : "请先添加视频源");
    if (!hasStream) {
        ui->label_inspectorHint->setText(
            "尚无视频源 · 请先从顶部添加文件、摄像头或 RTSP");
    } else if (!hasActiveStream) {
        ui->label_inspectorHint->setText(
            "尚无活动视频 · 请从左侧选择并设为主画面");
    } else {
        ui->label_inspectorHint->setText(
            "配置活动视频的预览、图像处理与视觉分析");
    }
    UpdateImageProcessingControls();
    UpdateVisionControls();
}

void MainWindow::AddMultiStreamFiles() {
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "选择多路视频文件",
        QString(),
        "视频文件 (*.mp4 *.mkv *.avi *.mov *.flv);;所有文件 (*.*)");

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
        RegisterStreamPresentationMetadata(
            streamId,
            QFileInfo(filePath).fileName(),
            StreamSourceKind::File,
            false);
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
    RegisterStreamPresentationMetadata(
        streamId,
        QString("%1（%2×%3 @ %4 FPS）")
            .arg(options.deviceName)
            .arg(options.width)
            .arg(options.height)
            .arg(options.frameRate),
        StreamSourceKind::Camera,
        true);

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
        "RTSP 地址：",
        QLineEdit::Normal,
        "rtsp://",
        &accepted).trimmed();
    if (!accepted) {
        return;
    }

    if (url.isEmpty()) {
        QMessageBox::warning(this, "网络流错误", "RTSP 地址不能为空");
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
    RegisterStreamPresentationMetadata(
        streamId,
        displayName,
        StreamSourceKind::Rtsp,
        true);
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
        RegisterStreamPresentationMetadata(
            activeStreamId,
            item.displayName,
            StreamSourceKind::File,
            false);
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


