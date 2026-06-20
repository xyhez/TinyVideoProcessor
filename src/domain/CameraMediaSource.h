#pragma once

#ifndef TINYVIDEOPROCESSOR_CAMERAMEDIASOURCE_H
#define TINYVIDEOPROCESSOR_CAMERAMEDIASOURCE_H

#include "IMediaSource.h"

/**
 * @brief CameraMediaSource 摄像头媒体源实现
 *
 * 负责通过 FFmpeg dshow (DirectShow) 打开和管理摄像头设备。
 * 第一版只支持 video-only，不接入摄像头音频。
 */
class CameraMediaSource : public IMediaSource {
public:
    /**
     * @brief CameraMediaSource 默认构造函数
     */
    CameraMediaSource() = default;

    /**
     * @brief CameraMediaSource 使用设备名构造摄像头媒体源
     * @param deviceName 摄像头设备名（如 "HD Pro Webcam C920"）
     */
    explicit CameraMediaSource(QString deviceName);

    /**
     * @brief ~CameraMediaSource 析构函数，释放摄像头资源
     */
    ~CameraMediaSource();

    /**
     * @brief Open 打开摄像头设备
     * @return true 打开成功
     * @return false 打开失败
     */
    bool Open() override;

    /**
     * @brief Close 关闭摄像头设备
     * @return true 关闭成功
     * @return false 关闭失败
     */
    bool Close() override;

    /**
     * @brief GetFormatContect 获取 FFmpeg 格式上下文
     * @return AVFormatContext 指针
     */
    AVFormatContext* GetFormatContect() override;

    /**
     * @brief GetType 获取媒体源类型
     * @return 摄像头媒体源类型
     */
    MediaSourceType GetType() override;

    /**
     * @brief GetSourceInfo 获取摄像头媒体源信息
     * @return 摄像头媒体源信息引用
     */
    Fileinfo& GetSourceInfo() override;

    /**
     * @brief SetInfo 设置摄像头设备名
     * @param deviceName 摄像头设备名
     */
    void SetInfo(QString deviceName) override;

    /**
     * @brief SetResolution 设置摄像头分辨率
     * @param width 宽度
     * @param height 高度
     */
    void SetResolution(int width, int height);

    /**
     * @brief SetFrameRate 设置摄像头帧率
     * @param frameRate 帧率
     */
    void SetFrameRate(int frameRate);

private:
    AVFormatContext* formatContext_ = nullptr;
    Fileinfo sourceInfo_;
    QString deviceName_;
    int width_ = 1280;
    int height_ = 720;
    int frameRate_ = 30;
};

#endif // TINYVIDEOPROCESSOR_CAMERAMEDIASOURCE_H
