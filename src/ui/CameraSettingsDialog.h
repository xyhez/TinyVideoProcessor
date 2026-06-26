#pragma once

#include <QDialog>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>

#include "domain/CameraTypes.h"

/**
 * @brief CameraSettingsDialog 摄像头设置对话框
 *
 * 让用户选择摄像头设备和参数（分辨率、帧率、格式）
 */
class CameraSettingsDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief CameraSettingsDialog 构造函数
     * @param parent 父窗口
     */
    explicit CameraSettingsDialog(QWidget* parent = nullptr);

    /**
     * @brief GetSelectedOptions 获取用户选择的打开选项
     * @return 摄像头打开选项
     */
    CameraOpenOptions GetSelectedOptions() const;

private slots:
    /**
     * @brief OnDeviceChanged 设备选择改变
     * @param index 选中的设备索引
     */
    void OnDeviceChanged(int index);

    /**
     * @brief OnFormatChanged 格式选择改变
     * @param index 选中的格式索引
     */
    void OnFormatChanged(int index);

    /**
     * @brief OnRefreshClicked 刷新按钮点击
     */
    void OnRefreshClicked();

private:
    /**
     * @brief CreateUI 创建 UI 控件
     */
    void CreateUI();

    /**
     * @brief LoadDevices 加载设备列表
     */
    void LoadDevices();

    /**
     * @brief LoadFormats 加载格式列表
     * @param deviceIndex 设备索引
     */
    void LoadFormats(int deviceIndex);

    /**
     * @brief UpdateFrameRates 更新帧率选择
     * @param formatIndex 格式索引
     */
    void UpdateFrameRates(int formatIndex);

    QComboBox* deviceComboBox_;	///< 设备下拉框
    QComboBox* formatComboBox_;	///< 格式下拉框
    QComboBox* frameRateComboBox_;	///< 帧率下拉框
    QPushButton* refreshButton_;	///< 刷新按钮
    QPushButton* okButton_;	///< 确认按钮
    QPushButton* cancelButton_;	///< 取消按钮

    QList<CameraDeviceInfo> devices_;	///< 摄像头设备列表
    QList<CameraFormatOption> currentFormats_;	///< 当前设备格式列表
};
