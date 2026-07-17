#include "CameraSettingsDialog.h"
#include "infrastructure/devices/CameraDeviceEnumerator.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

CameraSettingsDialog::CameraSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("摄像头设置");
    setMinimumWidth(500);

    CreateUI();
    LoadDevices();
}

CameraOpenOptions CameraSettingsDialog::GetSelectedOptions() const
{
    CameraOpenOptions options;

    // 获取选中的设备
    int deviceIndex = deviceComboBox_->currentIndex();
    if (deviceIndex < 0 || deviceIndex >= devices_.size()) {
        return options; // 返回无效选项
    }

    options.deviceName = devices_[deviceIndex].deviceName;

    // 获取选中的格式
    int formatIndex = formatComboBox_->currentIndex();
    if (formatIndex < 0 || formatIndex >= currentFormats_.size()) {
        return options; // 返回无效选项
    }

    const CameraFormatOption& format = currentFormats_[formatIndex];
    options.width = format.width;
    options.height = format.height;
    options.pixelFormat = format.pixelFormat;
    options.codec = format.codec;

    // 获取选中的帧率
    int frameRate = frameRateComboBox_->currentText().toInt();
    options.frameRate = frameRate > 0 ? frameRate : format.maxFrameRate;

    // 默认参数
    options.rtbufsize = 100;

    return options;
}

void CameraSettingsDialog::CreateUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 16);
    mainLayout->setSpacing(16);

    auto* introLabel = new QLabel(
        "选择本机摄像头与采集参数。刷新会重新枚举可用设备。",
        this);
    introLabel->setObjectName("cameraDialogIntro");
    introLabel->setWordWrap(true);
    mainLayout->addWidget(introLabel);

    // 设备选择组
    auto* deviceGroup = new QGroupBox("摄像头设备", this);
    deviceGroup->setObjectName("cameraDeviceGroup");
    auto* deviceLayout = new QVBoxLayout(deviceGroup);
    deviceLayout->setContentsMargins(12, 18, 12, 12);
    deviceLayout->setSpacing(8);

    auto* deviceRowLayout = new QHBoxLayout();
    deviceRowLayout->setSpacing(8);
    deviceComboBox_ = new QComboBox(this);
    deviceComboBox_->setObjectName("comboBox_cameraDevice");
    deviceComboBox_->setToolTip("选择要添加的本机摄像头");
    refreshButton_ = new QPushButton("刷新", this);
    refreshButton_->setObjectName("pushButton_refreshCameraDevices");
    refreshButton_->setToolTip("重新枚举本机摄像头设备");
    deviceRowLayout->addWidget(deviceComboBox_, 1);
    deviceRowLayout->addWidget(refreshButton_);

    deviceLayout->addLayout(deviceRowLayout);
    mainLayout->addWidget(deviceGroup);

    // 格式选择组
    auto* formatGroup = new QGroupBox("采集参数", this);
    formatGroup->setObjectName("cameraFormatGroup");
    auto* formatLayout = new QFormLayout(formatGroup);
    formatLayout->setContentsMargins(12, 18, 12, 12);
    formatLayout->setHorizontalSpacing(12);
    formatLayout->setVerticalSpacing(10);
    formatLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formatLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* formatLabel = new QLabel("视频格式", this);
    formatComboBox_ = new QComboBox(this);
    formatComboBox_->setObjectName("comboBox_cameraFormat");
    formatComboBox_->setToolTip("选择摄像头输出分辨率和编码格式");
    formatLayout->addRow(formatLabel, formatComboBox_);

    auto* frameRateLabel = new QLabel("采集帧率", this);
    frameRateComboBox_ = new QComboBox(this);
    frameRateComboBox_->setObjectName("comboBox_cameraFrameRate");
    frameRateComboBox_->setToolTip("选择摄像头采集帧率");
    formatLayout->addRow(frameRateLabel, frameRateComboBox_);

    mainLayout->addWidget(formatGroup);

    statusLabel_ = new QLabel("正在检测可用摄像头…", this);
    statusLabel_->setObjectName("cameraDialogStatus");
    statusLabel_->setWordWrap(true);
    mainLayout->addWidget(statusLabel_);

    // 按钮
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch();
    okButton_ = new QPushButton("确定", this);
    okButton_->setObjectName("pushButton_confirmCamera");
    okButton_->setToolTip("使用当前设备和参数添加摄像头");
    cancelButton_ = new QPushButton("取消", this);
    cancelButton_->setObjectName("pushButton_cancelCamera");
    cancelButton_->setToolTip("关闭对话框，不添加摄像头");
    buttonLayout->addWidget(okButton_);
    buttonLayout->addWidget(cancelButton_);

    mainLayout->addLayout(buttonLayout);

    setTabOrder(deviceComboBox_, refreshButton_);
    setTabOrder(refreshButton_, formatComboBox_);
    setTabOrder(formatComboBox_, frameRateComboBox_);
    setTabOrder(frameRateComboBox_, okButton_);
    setTabOrder(okButton_, cancelButton_);

    // 连接信号槽
    connect(deviceComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsDialog::OnDeviceChanged);
    connect(formatComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsDialog::OnFormatChanged);
    connect(refreshButton_, &QPushButton::clicked,
            this, &CameraSettingsDialog::OnRefreshClicked);
    connect(okButton_, &QPushButton::clicked,
            this, &QDialog::accept);
    connect(cancelButton_, &QPushButton::clicked,
            this, &QDialog::reject);
}

void CameraSettingsDialog::LoadDevices()
{
    devices_.clear();
    currentFormats_.clear();

    {
        const QSignalBlocker deviceBlocker(deviceComboBox_);
        const QSignalBlocker formatBlocker(formatComboBox_);
        const QSignalBlocker frameRateBlocker(frameRateComboBox_);
        deviceComboBox_->clear();
        formatComboBox_->clear();
        frameRateComboBox_->clear();
    }

    deviceComboBox_->setEnabled(false);
    formatComboBox_->setEnabled(false);
    frameRateComboBox_->setEnabled(false);
    okButton_->setEnabled(false);
    statusLabel_->setText("正在检测可用摄像头…");

    CameraDeviceEnumerator enumerator;
    devices_ = enumerator.EnumerateDevices();

    if (devices_.isEmpty()) {
        const QSignalBlocker blocker(deviceComboBox_);
        deviceComboBox_->addItem("未检测到可用摄像头");
        deviceComboBox_->setCurrentIndex(0);
        deviceComboBox_->setToolTip("未检测到可用摄像头");
        formatComboBox_->setToolTip("检测到摄像头后才能选择视频格式");
        frameRateComboBox_->setToolTip("检测到可用视频格式后才能选择采集帧率");
        okButton_->setToolTip("请先连接摄像头并刷新设备列表");
        statusLabel_->setText(
            "未检测到可用摄像头。请检查设备连接或系统权限，然后刷新。");
        return;
    }

    {
        const QSignalBlocker blocker(deviceComboBox_);
        for (const CameraDeviceInfo& device : devices_) {
            deviceComboBox_->addItem(device.DisplayName());
        }
        deviceComboBox_->setCurrentIndex(0);
    }

    deviceComboBox_->setEnabled(true);
    deviceComboBox_->setToolTip("选择要添加的本机摄像头");

    // 加载第一个设备的格式
    LoadFormats(0);
}

void CameraSettingsDialog::LoadFormats(int deviceIndex)
{
    currentFormats_.clear();

    {
        const QSignalBlocker formatBlocker(formatComboBox_);
        const QSignalBlocker frameRateBlocker(frameRateComboBox_);
        formatComboBox_->clear();
        frameRateComboBox_->clear();
    }

    formatComboBox_->setEnabled(false);
    frameRateComboBox_->setEnabled(false);
    okButton_->setEnabled(false);

    if (deviceIndex < 0 || deviceIndex >= devices_.size()) {
        formatComboBox_->setToolTip("请先选择可用的摄像头设备");
        frameRateComboBox_->setToolTip("请先选择可用的视频格式");
        okButton_->setToolTip("请先选择可用的摄像头设备");
        statusLabel_->setText("请选择一个可用的摄像头设备。");
        return;
    }

    const CameraDeviceInfo& device = devices_[deviceIndex];
    currentFormats_ = device.formats;

    if (currentFormats_.isEmpty()) {
        const QSignalBlocker blocker(formatComboBox_);
        formatComboBox_->addItem("无可用视频格式");
        formatComboBox_->setCurrentIndex(0);
        formatComboBox_->setToolTip("当前设备未报告可用视频格式");
        frameRateComboBox_->setToolTip("检测到可用视频格式后才能选择采集帧率");
        okButton_->setToolTip("当前设备没有可用视频格式");
        statusLabel_->setText(
            QString("设备“%1”未报告可用视频格式，请刷新或选择其他设备。")
                .arg(device.DisplayName()));
        return;
    }

    {
        const QSignalBlocker blocker(formatComboBox_);
        for (const CameraFormatOption& format : currentFormats_) {
            formatComboBox_->addItem(format.FormatDescription());
        }
        formatComboBox_->setCurrentIndex(0);
    }

    formatComboBox_->setEnabled(true);
    formatComboBox_->setToolTip("选择摄像头输出分辨率和编码格式");

    // 加载第一个格式的帧率
    UpdateFrameRates(0);
}

void CameraSettingsDialog::UpdateFrameRates(int formatIndex)
{
    {
        const QSignalBlocker blocker(frameRateComboBox_);
        frameRateComboBox_->clear();
    }

    frameRateComboBox_->setEnabled(false);
    okButton_->setEnabled(false);

    if (formatIndex < 0 || formatIndex >= currentFormats_.size()) {
        frameRateComboBox_->setToolTip("请先选择可用的视频格式");
        okButton_->setToolTip("请先选择可用的视频格式");
        statusLabel_->setText("请选择一个可用的视频格式。");
        return;
    }

    const CameraFormatOption& format = currentFormats_[formatIndex];

    {
        const QSignalBlocker blocker(frameRateComboBox_);

        // 生成帧率选项
        if (format.minFrameRate == format.maxFrameRate) {
            frameRateComboBox_->addItem(QString::number(format.maxFrameRate));
        } else {
            // 生成常用帧率：5, 10, 15, 24, 30
            const QList<int> commonRates = {5, 10, 15, 24, 30};
            for (int rate : commonRates) {
                if (rate >= format.minFrameRate && rate <= format.maxFrameRate) {
                    frameRateComboBox_->addItem(QString::number(rate));
                }
            }

            // 如果没有常用帧率，添加最大帧率
            if (frameRateComboBox_->count() == 0) {
                frameRateComboBox_->addItem(QString::number(format.maxFrameRate));
            }
        }

        // 默认选择最高的候选帧率
        const int maxRateIndex = frameRateComboBox_->count() - 1;
        if (maxRateIndex >= 0) {
            frameRateComboBox_->setCurrentIndex(maxRateIndex);
        }
    }

    const bool hasFrameRate = frameRateComboBox_->count() > 0;
    frameRateComboBox_->setEnabled(hasFrameRate);
    okButton_->setEnabled(hasFrameRate);
    if (!hasFrameRate) {
        frameRateComboBox_->setToolTip("当前视频格式没有可用采集帧率");
        okButton_->setToolTip("当前视频格式没有可用采集帧率");
        statusLabel_->setText("当前视频格式没有可用采集帧率，请选择其他格式。");
        return;
    }

    frameRateComboBox_->setToolTip("选择摄像头采集帧率");
    okButton_->setToolTip("使用当前设备和参数添加摄像头");
    statusLabel_->setText(
        QString("已检测到 %1 台摄像头。当前参数可用于添加视频源。")
            .arg(devices_.size()));
}

void CameraSettingsDialog::OnDeviceChanged(int index)
{
    LoadFormats(index);
}

void CameraSettingsDialog::OnFormatChanged(int index)
{
    UpdateFrameRates(index);
}

void CameraSettingsDialog::OnRefreshClicked()
{
    LoadDevices();
}
