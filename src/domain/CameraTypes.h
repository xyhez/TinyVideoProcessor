#pragma once

#include <QString>
#include <QList>

/**
 * @brief CameraFormatOption 摄像头格式选项
 *
 * 描述摄像头支持的一种输出格式（分辨率 + 帧率 + 编码格式组合）
 */
struct CameraFormatOption {
    int width = 0;	///< 宽度
    int height = 0;	///< 高度
    int minFrameRate = 0;	///< 最小帧率
    int maxFrameRate = 0;	///< 最大帧率
    QString pixelFormat;	///< 像素格式
    QString codec;	///< 编码格式

    /**
     * @brief FormatDescription 生成格式描述字符串
     * @return 格式描述，如 "1280x720 @ 30fps (H.264)"
     */
    QString FormatDescription() const {
        QString desc = QString("%1x%2").arg(width).arg(height);

        if (minFrameRate == maxFrameRate) {
            desc += QString(" @ %1fps").arg(maxFrameRate);
        } else {
            desc += QString(" @ %1-%2fps").arg(minFrameRate).arg(maxFrameRate);
        }

        if (!codec.isEmpty()) {
            desc += QString(" (%1)").arg(codec.toUpper());
        } else if (!pixelFormat.isEmpty()) {
            desc += QString(" (%1)").arg(pixelFormat.toUpper());
        }

        return desc;
    }

    /**
     * @brief IsValid 检查格式选项是否有效
     * @return true 有效，false 无效
     */
    bool IsValid() const {
        return width > 0 && height > 0 && maxFrameRate > 0;
    }
};

/**
 * @brief CameraDeviceInfo 摄像头设备信息
 *
 * 描述一个摄像头设备及其支持的格式列表
 */
struct CameraDeviceInfo {
    QString deviceName;	///< 设备名称
    QString friendlyName;	///< 友好名称
    QString description;	///< 设备描述
    QList<CameraFormatOption> formats;	///< 支持的格式列表

    /**
     * @brief IsValid 检查设备信息是否有效
     * @return true 有效，false 无效
     */
    bool IsValid() const {
        return !deviceName.isEmpty();
    }

    /**
     * @brief DisplayName 获取显示名称
     * @return 友好名称（如果有），否则返回设备名称
     */
    QString DisplayName() const {
        return friendlyName.isEmpty() ? deviceName : friendlyName;
    }
};

/**
 * @brief CameraOpenOptions 摄像头打开选项
 *
 * 封装用户选择的摄像头打开参数，传递给 CameraMediaSource
 */
struct CameraOpenOptions {
    QString deviceName;	///< 设备名称
    int width = 1280;	///< 打开宽度
    int height = 720;	///< 打开高度
    int frameRate = 30;	///< 打开帧率
    QString pixelFormat;	///< 像素格式
    QString codec;	///< 编码格式
    int rtbufsize = 100;	///< 实时缓冲区大小，单位 MB

    /**
     * @brief IsValid 检查打开选项是否有效
     * @return true 有效，false 无效
     */
    bool IsValid() const {
        return !deviceName.isEmpty()
            && width > 0
            && height > 0
            && frameRate > 0;
    }

    /**
     * @brief FromFormatOption 从格式选项创建打开选项
     * @param deviceName 设备名称
     * @param format 格式选项
     * @param targetFrameRate 目标帧率（默认使用最大帧率）
     * @return 打开选项
     */
    static CameraOpenOptions FromFormatOption(
        const QString& deviceName,
        const CameraFormatOption& format,
        int targetFrameRate = -1)
    {
        CameraOpenOptions options;
        options.deviceName = deviceName;
        options.width = format.width;
        options.height = format.height;
        options.frameRate = targetFrameRate > 0 ? targetFrameRate : format.maxFrameRate;
        options.pixelFormat = format.pixelFormat;
        options.codec = format.codec;
        return options;
    }

    /**
     * @brief Description 生成选项描述字符串
     * @return 选项描述，如 "HD Pro Webcam C920: 1280x720 @ 30fps"
     */
    QString Description() const {
        return QString("%1: %2x%3 @ %4fps")
            .arg(deviceName)
            .arg(width)
            .arg(height)
            .arg(frameRate);
    }
};
