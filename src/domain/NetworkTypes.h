#pragma once

#ifndef TINYVIDEOPROCESSOR_NETWORKTYPES_H
#define TINYVIDEOPROCESSOR_NETWORKTYPES_H

#include <QString>

/**
 * @brief NetworkStreamProtocol 网络流协议类型
 */
enum class NetworkStreamProtocol {
    Auto,	///< 自动根据 URL 推断协议
    Rtsp,	///< RTSP 网络流
    Rtmp,	///< RTMP 网络流，当前仅预留
    HttpFlv,	///< HTTP-FLV 网络流，当前仅预留
    Mjpeg	///< MJPEG 网络流，当前仅预留
};

/**
 * @brief NetworkOpenOptions 网络流打开参数
 */
struct NetworkOpenOptions {
    QString url;	///< 网络流 URL
    NetworkStreamProtocol protocol = NetworkStreamProtocol::Auto;	///< 网络流协议
    QString rtspTransport = "tcp";	///< RTSP 传输方式，默认 tcp
    int openTimeoutMs = 5000;	///< 打开超时时间，单位毫秒
    int readTimeoutMs = 5000;	///< 读取超时时间，单位毫秒
    int bufferSizeBytes = 1024 * 1024;	///< 网络缓冲区大小，单位字节
    int maxDelayUs = 500000;	///< 最大延迟，单位微秒
    bool lowLatency = true;	///< 是否启用低延迟选项
};

#endif // TINYVIDEOPROCESSOR_NETWORKTYPES_H
