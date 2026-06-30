#pragma once

#ifndef TINYVIDEOPROCESSOR_NETWORKMEDIASOURCE_H
#define TINYVIDEOPROCESSOR_NETWORKMEDIASOURCE_H

#include "IMediaSource.h"
#include "NetworkTypes.h"

#include <atomic>

/**
 * @brief NetworkMediaSource 网络流媒体源实现
 *
 * 负责通过 FFmpeg 打开和关闭网络媒体流。
 * 第一版只把 RTSP 作为真实支持目标，RTMP/HTTP-FLV/MJPEG 仅保留协议枚举。
 */
class NetworkMediaSource : public IMediaSource {
public:
    /**
     * @brief NetworkMediaSource 默认构造函数
     */
    NetworkMediaSource() = default;

    /**
     * @brief NetworkMediaSource 使用 URL 构造网络媒体源
     * @param url 网络流 URL
     */
    explicit NetworkMediaSource(QString url);

    /**
     * @brief NetworkMediaSource 使用打开参数构造网络媒体源
     * @param options 网络流打开参数
     */
    explicit NetworkMediaSource(const NetworkOpenOptions& options);

    /**
     * @brief ~NetworkMediaSource 析构函数，释放网络流资源
     */
    ~NetworkMediaSource();

    /**
     * @brief Open 打开网络媒体流
     * @return true 打开成功
     * @return false 打开失败
     */
    bool Open() override;

    /**
     * @brief Close 关闭网络媒体流
     * @return true 关闭成功
     * @return false 当前没有已打开资源
     */
    bool Close() override;

    /**
     * @brief GetFormatContect 获取 FFmpeg 格式上下文
     * @return AVFormatContext 指针
     */
    AVFormatContext* GetFormatContect() override;

    /**
     * @brief GetType 获取媒体源类型
     * @return 网络媒体源类型
     */
    MediaSourceType GetType() override;

    /**
     * @brief SetInfo 设置网络流 URL
     * @param sourceInfo 网络流 URL
     */
    void SetInfo(QString sourceInfo) override;

    /**
     * @brief GetSourceInfo 获取网络媒体源信息
     * @return 网络媒体源信息引用
     */
    Fileinfo& GetSourceInfo() override;

    /**
     * @brief SetOpenOptions 设置网络流打开参数
     * @param options 网络流打开参数
     */
    void SetOpenOptions(const NetworkOpenOptions& options);

    /**
     * @brief LastError 获取最近一次网络流操作失败原因
     * @return 错误信息
     */
    QString LastError() const override;

    /**
     * @brief SourceIdentity 获取网络流稳定身份
     * @return 网络流身份
     */
    QString SourceIdentity() const override;

    /**
     * @brief AllowsDuplicateOpen 网络流默认不允许重复打开同一 URL
     * @return false
     */
    bool AllowsDuplicateOpen() const override;

    /**
     * @brief RequestInterrupt 请求中断当前网络阻塞读
     */
    void RequestInterrupt() override;

    /**
     * @brief ClearInterrupt 清除网络阻塞读中断请求
     */
    void ClearInterrupt() override;

    /**
     * @brief IsSeekable 网络实时流默认不支持 seek
     * @return false
     */
    bool IsSeekable() const override;

    /**
     * @brief HasDuration 网络实时流默认没有确定时长
     * @return false
     */
    bool HasDuration() const override;

    /**
     * @brief IsRealtime 网络流默认按实时源处理
     * @return true
     */
    bool IsRealtime() const override;

private:
    /**
     * @brief SetLastError 保存最近一次失败原因并输出日志
     * @param message 错误信息
     */
    void SetLastError(const QString& message);

    /**
     * @brief IsProtocolSupported 判断当前协议是否已支持
     * @return true 支持打开
     * @return false 当前仅预留
     */
    bool IsProtocolSupported() const;

    /**
     * @brief ConfigureOpenOptions 配置 FFmpeg 打开参数
     * @param dictionary FFmpeg 字典指针
     */
    void ConfigureOpenOptions(AVDictionary** dictionary) const;

    /**
     * @brief InterruptCallback FFmpeg 阻塞操作中断回调
     * @param opaque NetworkMediaSource 实例指针
     * @return 1 请求中断，0 继续等待
     */
    static int InterruptCallback(void* opaque);

    AVFormatContext* formatContext_ = nullptr;	///< FFmpeg 格式上下文
    Fileinfo sourceInfo_;	///< 网络媒体源信息
    NetworkOpenOptions options_;	///< 网络流打开参数
    QString lastError_;	///< 最近一次错误信息
    std::atomic<bool> interruptRequested_ = false;	///< 是否请求中断 FFmpeg 阻塞操作
};

#endif // TINYVIDEOPROCESSOR_NETWORKMEDIASOURCE_H
