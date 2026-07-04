#pragma once

#include "domain/CameraTypes.h"
#include <QList>

/**
 * @brief CameraDeviceEnumerator 摄像头设备枚举服务
 *
 * 基础设施层服务，负责枚举系统摄像头设备及其支持的格式选项。
 *
 * 职责：
 * - 枚举系统可用的摄像头设备
 * - 查询每个设备支持的格式选项（分辨率、帧率、编码格式）
 * - 不创建 MediaPipeline
 * - 不打开播放流
 *
 * 第一版使用 FFmpeg dshow API 实现。
 */
class CameraDeviceEnumerator {
public:
    /**
     * @brief CameraDeviceEnumerator 默认构造函数
     */
    CameraDeviceEnumerator() = default;

    /**
     * @brief ~CameraDeviceEnumerator 析构函数
     */
    ~CameraDeviceEnumerator() = default;

    /**
     * @brief EnumerateDevices 枚举系统可用的摄像头设备
     * @return 摄像头设备列表
     */
    QList<CameraDeviceInfo> EnumerateDevices();

    /**
     * @brief EnumerateDeviceFormats 枚举指定设备支持的格式选项
     * @param deviceName 设备名称
     * @return 格式选项列表
     */
    QList<CameraFormatOption> EnumerateDeviceFormats(const QString& deviceName);

private:
    /**
     * @brief ParseFormatFromOutput 从 FFmpeg 输出解析格式选项
     * @param output FFmpeg 输出文本
     * @return 格式选项列表
     */
    QList<CameraFormatOption> ParseFormatFromOutput(const QString& output);

    /**
     * @brief GetHardcodedC920Formats 获取 C920 硬编码的格式列表
     * @return C920 格式选项列表
     *
     * 第一版使用硬编码的 C920 格式列表，避免复杂的 FFmpeg 输出解析。
     * Phase 2.2.4 或后续可以改为动态查询。
     */
    QList<CameraFormatOption> GetHardcodedC920Formats();
};
