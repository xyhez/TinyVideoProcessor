#include "CameraDeviceEnumerator.h"

#include <QDebug>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
}

QList<CameraDeviceInfo> CameraDeviceEnumerator::EnumerateDevices()
{
    QList<CameraDeviceInfo> devices;

    // 注册 FFmpeg 设备（libavdevice）
    static bool devicesRegistered = false;
    if (!devicesRegistered) {
        avdevice_register_all();
        devicesRegistered = true;
    }

    // 查找 dshow input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        qWarning() << "CameraDeviceEnumerator::EnumerateDevices() - dshow input format not found";
        return devices;
    }

    // 枚举设备列表
    AVDeviceInfoList* deviceList = nullptr;
    int ret = avdevice_list_input_sources(inputFormat, nullptr, nullptr, &deviceList);
    if (ret < 0 || !deviceList) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qWarning() << "CameraDeviceEnumerator::EnumerateDevices() - Failed to enumerate devices:" << errbuf;
        return devices;
    }

    qDebug() << "CameraDeviceEnumerator::EnumerateDevices() - Found" << deviceList->nb_devices << "devices";

    // 遍历设备列表
    for (int i = 0; i < deviceList->nb_devices; i++) {
        AVDeviceInfo* device = deviceList->devices[i];

        CameraDeviceInfo info;
        info.deviceName = QString::fromUtf8(device->device_name);
        info.friendlyName = QString::fromUtf8(device->device_description);
        info.description = info.friendlyName; // 暂时使用友好名称作为描述

        qDebug() << "  Device" << i << ":" << info.deviceName;
        qDebug() << "    Description:" << info.friendlyName;

        // 查询设备格式选项（第一版暂不实现，返回空列表）
        // info.formats = EnumerateDeviceFormats(info.deviceName);

        devices.append(info);
    }

    avdevice_free_list_devices(&deviceList);

    return devices;
}

QList<CameraFormatOption> CameraDeviceEnumerator::EnumerateDeviceFormats(const QString& deviceName)
{
    QList<CameraFormatOption> formats;

    // 第一版暂不实现格式枚举
    // 格式枚举需要打开设备查询能力，实现较复杂
    // 可以在 Phase 2.2.3 或后续实现

    qDebug() << "CameraDeviceEnumerator::EnumerateDeviceFormats() - Not implemented yet";

    return formats;
}

QList<CameraFormatOption> CameraDeviceEnumerator::ParseFormatFromOutput(const QString& output)
{
    QList<CameraFormatOption> formats;

    // 第一版暂不实现
    // 需要解析 FFmpeg -list_options 输出
    // 格式示例：
    //   [dshow @ ...] Pin "Capture" (alternative pin name "捕获")
    //   [dshow @ ...]   vcodec=h264  min s=640x480 fps=15 max s=1920x1080 fps=30
    //   [dshow @ ...]   pixel_format=yuyv422  min s=640x480 fps=5 max s=1920x1080 fps=30

    Q_UNUSED(output);

    return formats;
}
