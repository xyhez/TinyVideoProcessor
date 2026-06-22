#include "CameraDeviceEnumerator.h"
#include "core/AppLog.h"

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
        APP_LOG_WARN("CameraDeviceEnumerator::EnumerateDevices() - dshow input format not found");
        return devices;
    }

    // 枚举设备列表
    AVDeviceInfoList* deviceList = nullptr;
    int ret = avdevice_list_input_sources(inputFormat, nullptr, nullptr, &deviceList);
    if (ret < 0 || !deviceList) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        APP_LOG_WARN("CameraDeviceEnumerator::EnumerateDevices() - Failed to enumerate devices:", errbuf);
        return devices;
    }

    APP_LOG_DEBUG("CameraDeviceEnumerator::EnumerateDevices() - Found", deviceList->nb_devices, "devices");

    // 遍历设备列表
    for (int i = 0; i < deviceList->nb_devices; i++) {
        AVDeviceInfo* device = deviceList->devices[i];

        CameraDeviceInfo info;
        // device_name 是 PnP 路径或唯一标识符
        // device_description 是友好名称
        // dshow 可以使用友好名称打开设备，更简单
        info.deviceName = QString::fromUtf8(device->device_description);
        info.friendlyName = QString::fromUtf8(device->device_description);
        info.description = info.friendlyName;

        APP_LOG_DEBUG("  Device", i, ":", info.deviceName);
        APP_LOG_DEBUG("    PnP Path:", QString::fromUtf8(device->device_name));
        APP_LOG_DEBUG("    Description:", info.friendlyName);

        // 查询设备格式选项
        info.formats = EnumerateDeviceFormats(info.deviceName);
        APP_LOG_DEBUG("    Formats:", info.formats.size());

        devices.append(info);
    }

    avdevice_free_list_devices(&deviceList);

    return devices;
}

QList<CameraFormatOption> CameraDeviceEnumerator::EnumerateDeviceFormats(const QString& deviceName)
{
    QList<CameraFormatOption> formats;

    APP_LOG_DEBUG("CameraDeviceEnumerator::EnumerateDeviceFormats() - Querying formats for:", deviceName);

    // 注册 FFmpeg 设备
    static bool devicesRegistered = false;
    if (!devicesRegistered) {
        avdevice_register_all();
        devicesRegistered = true;
    }

    // 查找 dshow input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        APP_LOG_WARN("CameraDeviceEnumerator::EnumerateDeviceFormats() - dshow input format not found");
        return formats;
    }

    // 构造设备 URL
    QString deviceUrl = QString("video=%1").arg(deviceName);

    // 设置选项：list_options=true 会让 dshow 输出设备支持的格式
    AVDictionary* options = nullptr;
    av_dict_set(&options, "list_options", "true", 0);

    AVFormatContext* formatContext = nullptr;

    // 尝试打开设备（会失败，但会输出格式信息到日志）
    // 注意：这里会输出大量日志到 stderr
    int ret = avformat_open_input(&formatContext, deviceUrl.toUtf8().constData(),
                                   inputFormat, &options);

    av_dict_free(&options);

    if (formatContext) {
        avformat_close_input(&formatContext);
    }

    // FFmpeg 的 list_options 会将格式信息输出到 stderr
    // 但无法直接捕获，需要通过 QProcess 执行 ffmpeg 命令来获取输出

    APP_LOG_WARN("CameraDeviceEnumerator::EnumerateDeviceFormats() - FFmpeg list_options output goes to stderr");
    APP_LOG_WARN("CameraDeviceEnumerator::EnumerateDeviceFormats() - Format enumeration via API is complex");
    APP_LOG_WARN("CameraDeviceEnumerator::EnumerateDeviceFormats() - Returning hardcoded C920 formats for Phase 2.2.3");

    // 第一版：返回 C920 硬编码的常用格式
    // Phase 2.2.4 可以改为解析 ffmpeg 命令输出
    formats = GetHardcodedC920Formats();

    return formats;
}

QList<CameraFormatOption> CameraDeviceEnumerator::GetHardcodedC920Formats()
{
    QList<CameraFormatOption> formats;

    // C920 支持的 H.264 格式
    CameraFormatOption h264_1080p30;
    h264_1080p30.width = 1920;
    h264_1080p30.height = 1080;
    h264_1080p30.minFrameRate = 15;
    h264_1080p30.maxFrameRate = 30;
    h264_1080p30.codec = "h264";
    h264_1080p30.pixelFormat = ""; // 让 FFmpeg 自动选择
    formats.append(h264_1080p30);

    CameraFormatOption h264_720p30;
    h264_720p30.width = 1280;
    h264_720p30.height = 720;
    h264_720p30.minFrameRate = 15;
    h264_720p30.maxFrameRate = 30;
    h264_720p30.codec = "h264";
    h264_720p30.pixelFormat = ""; // 让 FFmpeg 自动选择
    formats.append(h264_720p30);

    CameraFormatOption h264_480p30;
    h264_480p30.width = 640;
    h264_480p30.height = 480;
    h264_480p30.minFrameRate = 15;
    h264_480p30.maxFrameRate = 30;
    h264_480p30.codec = "h264";
    h264_480p30.pixelFormat = ""; // 让 FFmpeg 自动选择
    formats.append(h264_480p30);

    // C920 支持的 YUYV422 格式（raw）
    CameraFormatOption yuyv_720p10;
    yuyv_720p10.width = 1280;
    yuyv_720p10.height = 720;
    yuyv_720p10.minFrameRate = 5;
    yuyv_720p10.maxFrameRate = 10;
    yuyv_720p10.codec = "";
    yuyv_720p10.pixelFormat = "yuyv422";
    formats.append(yuyv_720p10);

    CameraFormatOption yuyv_1080p5;
    yuyv_1080p5.width = 1920;
    yuyv_1080p5.height = 1080;
    yuyv_1080p5.minFrameRate = 5;
    yuyv_1080p5.maxFrameRate = 5;
    yuyv_1080p5.codec = "";
    yuyv_1080p5.pixelFormat = "yuyv422";
    formats.append(yuyv_1080p5);

    APP_LOG_DEBUG("CameraDeviceEnumerator::GetHardcodedC920Formats() - Returning", formats.size(), "formats");

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
