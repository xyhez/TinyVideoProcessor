#include "videoframeconverter.h"

#include "core/AppLog.h"

#include <QString>
#include <utility>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {
constexpr AVPixelFormat kDisplayPixelFormat = AV_PIX_FMT_YUV420P;

QString PixelFormatName(AVPixelFormat format)
{
    const char* name = av_get_pix_fmt_name(format);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(format));
}
}

VideoFrameConverter::~VideoFrameConverter()
{
    if (swsContext_) {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }
}

AVFramePtr VideoFrameConverter::ConvertToDisplayFrame(AVFramePtr frame)
{
    if (!frame) {
        return {};
    }

    if (IsDisplayFormat(frame.get())) {
        return std::move(frame);
    }

    return ConvertSoftwareFrame(std::move(frame));
}

bool VideoFrameConverter::IsDisplayFormat(const AVFrame* frame) const
{
    return frame && frame->format == kDisplayPixelFormat;
}

bool VideoFrameConverter::IsHardwareFrameFormat(AVPixelFormat format) const
{
    const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
    return descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

AVFramePtr VideoFrameConverter::ConvertSoftwareFrame(AVFramePtr frame)
{
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return {};
    }

    const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(frame->format);
    if (sourceFormat == AV_PIX_FMT_NONE || IsHardwareFrameFormat(sourceFormat)) {
        LogConversionIfNeeded(sourceFormat, frame->width, frame->height, false);
        return {};
    }

    AVFramePtr convertedFrame = MakeAVFrame();
    if (!convertedFrame) {
        LogConversionIfNeeded(sourceFormat, frame->width, frame->height, false);
        return {};
    }

    convertedFrame->format = kDisplayPixelFormat;
    convertedFrame->width = frame->width;
    convertedFrame->height = frame->height;

    int ret = av_frame_get_buffer(convertedFrame.get(), 32);
    if (ret < 0) {
        LogConversionIfNeeded(sourceFormat, frame->width, frame->height, false);
        return {};
    }

    av_frame_copy_props(convertedFrame.get(), frame.get());
    if (convertedFrame->time_base.den <= 0) {
        convertedFrame->time_base = frame->time_base;
    }

    swsContext_ = sws_getCachedContext(swsContext_,
                                       frame->width,
                                       frame->height,
                                       sourceFormat,
                                       convertedFrame->width,
                                       convertedFrame->height,
                                       kDisplayPixelFormat,
                                       SWS_BILINEAR,
                                       nullptr,
                                       nullptr,
                                       nullptr);
    if (!swsContext_) {
        LogConversionIfNeeded(sourceFormat, frame->width, frame->height, false);
        return {};
    }

    ret = sws_scale(swsContext_,
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    convertedFrame->data,
                    convertedFrame->linesize);
    if (ret <= 0) {
        LogConversionIfNeeded(sourceFormat, frame->width, frame->height, false);
        return {};
    }

    LogConversionIfNeeded(sourceFormat, frame->width, frame->height, true);
    return convertedFrame;
}

void VideoFrameConverter::LogConversionIfNeeded(AVPixelFormat sourceFormat,
                                                int width,
                                                int height,
                                                bool success)
{
    const bool changed = !hasLoggedConversion_
        || lastLoggedSourceFormat_ != sourceFormat
        || lastLoggedWidth_ != width
        || lastLoggedHeight_ != height
        || lastLoggedSuccess_ != success;
    if (!changed) {
        return;
    }

    hasLoggedConversion_ = true;
    lastLoggedSourceFormat_ = sourceFormat;
    lastLoggedWidth_ = width;
    lastLoggedHeight_ = height;
    lastLoggedSuccess_ = success;

    if (success) {
        APP_LOG_INFO_STREAM().noquote()
            << "VideoFrameConverter format conversion:"
            << PixelFormatName(sourceFormat)
            << "->" << PixelFormatName(kDisplayPixelFormat)
            << QString("size=%1x%2").arg(width).arg(height);
        return;
    }

    APP_LOG_WARN_STREAM().noquote()
        << "VideoFrameConverter format conversion failed:"
        << PixelFormatName(sourceFormat)
        << "->" << PixelFormatName(kDisplayPixelFormat)
        << QString("size=%1x%2").arg(width).arg(height);
}
