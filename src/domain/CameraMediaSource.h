#pragma once

#ifndef TINYVIDEOPROCESSOR_CAMERAMEDIASOURCE_H
#define TINYVIDEOPROCESSOR_CAMERAMEDIASOURCE_H

#include "IMediaSource.h"
#include "CameraTypes.h"

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
     * @brief CameraMediaSource 使用打开选项构造摄像头媒体源
     * @param options 摄像头打开选项
     */
    explicit CameraMediaSource(const CameraOpenOptions& options);

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

    /**
     * @brief SetOpenOptions 设置打开选项
     * @param options 摄像头打开选项
     */
    void SetOpenOptions(const CameraOpenOptions& options);

    /**
     * @brief LastError 获取最近一次摄像头打开或读取信息失败原因
     * @return 错误信息
     */
    QString LastError() const override;

    /**
     * @brief SourceIdentity 获取摄像头源身份
     * @return 摄像头源身份
     */
    QString SourceIdentity() const override;

    /**
     * @brief AllowsDuplicateOpen 摄像头设备不允许重复打开
     * @return false
     */
    bool AllowsDuplicateOpen() const override;

    /**
     * @brief IsSeekable 摄像头不支持 seek
     * @return false
     */
    bool IsSeekable() const override;

    /**
     * @brief HasDuration 摄像头没有确定的时长
     * @return false
     */
    bool HasDuration() const override;

    /**
     * @brief IsRealtime 摄像头是实时源
     * @return true
     */
    bool IsRealtime() const override;

private:
    /**
     * @brief SetLastError 保存最近一次失败原因并输出日志
     * @param message 错误信息
     */
    void SetLastError(const QString& message);

    AVFormatContext* formatContext_ = nullptr;
    Fileinfo sourceInfo_;
    QString lastError_;
    QString deviceName_;
    int width_ = 1280;
    int height_ = 720;
    int frameRate_ = 30;
    int rtbufsize_ = 100;       ///< rtbufsize (MB)
    QString pixelFormat_;       ///< 像素格式（可选）
    QString codec_;             ///< 编码格式（可选）
};

#endif // TINYVIDEOPROCESSOR_CAMERAMEDIASOURCE_H
