#include "CameraSettingsDialog.h"
#include "infrastructure/devices/CameraDeviceEnumerator.h"

#include <QMessageBox>

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
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 设备选择组
    QGroupBox* deviceGroup = new QGroupBox("设备", this);
    QVBoxLayout* deviceLayout = new QVBoxLayout(deviceGroup);

    QHBoxLayout* deviceRowLayout = new QHBoxLayout();
    deviceComboBox_ = new QComboBox(this);
    refreshButton_ = new QPushButton("刷新", this);
    refreshButton_->setMaximumWidth(80);
    deviceRowLayout->addWidget(deviceComboBox_);
    deviceRowLayout->addWidget(refreshButton_);

    deviceLayout->addLayout(deviceRowLayout);
    mainLayout->addWidget(deviceGroup);

    // 格式选择组
    QGroupBox* formatGroup = new QGroupBox("视频格式", this);
    QVBoxLayout* formatLayout = new QVBoxLayout(formatGroup);

    QHBoxLayout* formatRowLayout = new QHBoxLayout();
    QLabel* formatLabel = new QLabel("分辨率和编码:", this);
    formatComboBox_ = new QComboBox(this);
    formatRowLayout->addWidget(formatLabel);
    formatRowLayout->addWidget(formatComboBox_, 1);
    formatLayout->addLayout(formatRowLayout);

    QHBoxLayout* frameRateRowLayout = new QHBoxLayout();
    QLabel* frameRateLabel = new QLabel("帧率:", this);
    frameRateComboBox_ = new QComboBox(this);
    frameRateRowLayout->addWidget(frameRateLabel);
    frameRateRowLayout->addWidget(frameRateComboBox_, 1);
    formatLayout->addLayout(frameRateRowLayout);

    mainLayout->addWidget(formatGroup);

    // 按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    okButton_ = new QPushButton("确定", this);
    cancelButton_ = new QPushButton("取消", this);
    buttonLayout->addWidget(okButton_);
    buttonLayout->addWidget(cancelButton_);

    mainLayout->addLayout(buttonLayout);

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
    deviceComboBox_->clear();
    devices_.clear();

    CameraDeviceEnumerator enumerator;
    devices_ = enumerator.EnumerateDevices();

    if (devices_.isEmpty()) {
        deviceComboBox_->addItem("(未找到摄像头)");
        formatComboBox_->setEnabled(false);
        frameRateComboBox_->setEnabled(false);
        okButton_->setEnabled(false);
        return;
    }

    for (const CameraDeviceInfo& device : devices_) {
        deviceComboBox_->addItem(device.DisplayName());
    }

    formatComboBox_->setEnabled(true);
    frameRateComboBox_->setEnabled(true);
    okButton_->setEnabled(true);

    // 加载第一个设备的格式
    if (!devices_.isEmpty()) {
        LoadFormats(0);
    }
}

void CameraSettingsDialog::LoadFormats(int deviceIndex)
{
    formatComboBox_->clear();
    currentFormats_.clear();

    if (deviceIndex < 0 || deviceIndex >= devices_.size()) {
        return;
    }

    const CameraDeviceInfo& device = devices_[deviceIndex];
    currentFormats_ = device.formats;

    if (currentFormats_.isEmpty()) {
        formatComboBox_->addItem("(无可用格式)");
        frameRateComboBox_->setEnabled(false);
        okButton_->setEnabled(false);
        return;
    }

    for (const CameraFormatOption& format : currentFormats_) {
        formatComboBox_->addItem(format.FormatDescription());
    }

    frameRateComboBox_->setEnabled(true);
    okButton_->setEnabled(true);

    // 加载第一个格式的帧率
    if (!currentFormats_.isEmpty()) {
        UpdateFrameRates(0);
    }
}

void CameraSettingsDialog::UpdateFrameRates(int formatIndex)
{
    frameRateComboBox_->clear();

    if (formatIndex < 0 || formatIndex >= currentFormats_.size()) {
        return;
    }

    const CameraFormatOption& format = currentFormats_[formatIndex];

    // 生成帧率选项
    if (format.minFrameRate == format.maxFrameRate) {
        frameRateComboBox_->addItem(QString::number(format.maxFrameRate));
    } else {
        // 生成常用帧率：5, 10, 15, 24, 30
        QList<int> commonRates = {5, 10, 15, 24, 30};
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

    // 默认选择最高帧率
    int maxRateIndex = frameRateComboBox_->count() - 1;
    if (maxRateIndex >= 0) {
        frameRateComboBox_->setCurrentIndex(maxRateIndex);
    }
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
