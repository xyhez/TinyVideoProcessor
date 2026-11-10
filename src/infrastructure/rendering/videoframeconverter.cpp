#include "videoframeconverter.h"

#include "tiny_log/Logging.h"

#include <QString>
#include <utility>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {
constexpr AVPixelFormat kDisplayPixelFormat = AV_PIX_FMT_YUV420P;	///< MultiViewRenderer 当前统一接收的软件显示像素格式
constexpr AVColorRange kDisplayColorRange = AVCOL_RANGE_JPEG;	///< 转换后显示帧使用的全范围色彩范围标记
constexpr AVColorSpace kDisplayColorSpace = AVCOL_SPC_SMPTE170M;	///< 转换后显示帧使用的默认 SDR 色彩空间
constexpr AVColorPrimaries kDisplayColorPrimaries = AVCOL_PRI_SMPTE170M;	///< 转换后显示帧使用的默认 SDR 色彩基准
constexpr AVColorTransferCharacteristic kDisplayColorTransfer = AVCOL_TRC_BT709;	///< 转换后显示帧使用的默认传输曲线
constexpr int kDisplaySwsColorspace = SWS_CS_SMPTE170M;	///< swscale 转换输出侧使用的色彩矩阵
constexpr int kFullRangeFlag = 1;	///< swscale 色彩范围参数中的全范围标记
constexpr int kLimitedRangeFlag = 0;	///< swscale 色彩范围参数中的有限范围标记
constexpr int kFixedPointIdentity = 1 << 16;	///< swscale 亮度/对比度/饱和度的定点恒等系数
constexpr int kMaxLoggedConversionSignatures = 64;	///< 已记录格式转换日志签名的最大缓存数量

struct SwsColorProfile {
    AVColorRange range = AVCOL_RANGE_UNSPECIFIED;
    AVColorSpace colorSpace = AVCOL_SPC_UNSPECIFIED;
    int swsColorspace = SWS_CS_DEFAULT;
    bool usedFallback = false;
    bool usedBt2020Fallback = false;
};

QString PixelFormatName(AVPixelFormat format)
{
    const char* name = av_get_pix_fmt_name(format);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(format));
}

QString ColorRangeName(AVColorRange range)
{
    const char* name = av_color_range_name(range);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(range));
}

QString ColorSpaceName(AVColorSpace colorSpace)
{
    const char* name = av_color_space_name(colorSpace);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(colorSpace));
}

bool IsJpegRangePixelFormat(AVPixelFormat format)
{
    return format == AV_PIX_FMT_YUVJ420P
        || format == AV_PIX_FMT_YUVJ422P
        || format == AV_PIX_FMT_YUVJ444P
        || format == AV_PIX_FMT_YUVJ440P
        || format == AV_PIX_FMT_YUVJ411P;
}

int RangeFlag(AVColorRange range)
{
    return range == AVCOL_RANGE_JPEG ? kFullRangeFlag : kLimitedRangeFlag;
}

SwsColorProfile ResolveSourceColorProfile(const AVFrame* frame, AVPixelFormat sourceFormat)
{
    SwsColorProfile profile;

    if (frame && frame->color_range == AVCOL_RANGE_JPEG) {
        profile.range = AVCOL_RANGE_JPEG;
    } else if (frame && frame->color_range == AVCOL_RANGE_MPEG) {
        profile.range = AVCOL_RANGE_MPEG;
    } else if (IsJpegRangePixelFormat(sourceFormat)) {
        profile.range = AVCOL_RANGE_JPEG;
        profile.usedFallback = true;
    } else {
        profile.range = AVCOL_RANGE_MPEG;
        profile.usedFallback = true;
    }

    const AVColorSpace colorSpace = frame ? frame->colorspace : AVCOL_SPC_UNSPECIFIED;
    switch (colorSpace) {
    case AVCOL_SPC_BT709:
        profile.colorSpace = AVCOL_SPC_BT709;
        profile.swsColorspace = SWS_CS_ITU709;
        break;
    case AVCOL_SPC_FCC:
        profile.colorSpace = AVCOL_SPC_FCC;
        profile.swsColorspace = SWS_CS_FCC;
        break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        profile.colorSpace = colorSpace;
        profile.swsColorspace = SWS_CS_SMPTE170M;
        break;
    case AVCOL_SPC_SMPTE240M:
        profile.colorSpace = AVCOL_SPC_SMPTE240M;
        profile.swsColorspace = SWS_CS_SMPTE240M;
        break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        profile.colorSpace = AVCOL_SPC_BT709;
        profile.swsColorspace = SWS_CS_ITU709;
        profile.usedFallback = true;
        profile.usedBt2020Fallback = true;
        break;
    default:
        profile.colorSpace = frame && frame->width >= 1280 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        profile.swsColorspace = frame && frame->width >= 1280 ? SWS_CS_ITU709 : SWS_CS_SMPTE170M;
        profile.usedFallback = true;
        break;
    }

    return profile;
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

    // YUV420P 直接交给 shader，由 shader 处理 range/BT.601/BT.709；
    // 不再因为色彩元数据与默认值不同而做一次无意义的 YUV→YUV 转换。
    if (IsDisplayFormat(frame.get())) {
        return std::move(frame);
    }

    return ConvertSoftwareFrame(std::move(frame));
}

bool VideoFrameConverter::IsDisplayFormat(const AVFrame* frame) const
{
    return frame && frame->format == kDisplayPixelFormat;
}

bool VideoFrameConverter::HasDisplayColorProfile(const AVFrame* frame) const
{
    return frame
        && frame->color_range == kDisplayColorRange
        && frame->colorspace == kDisplayColorSpace;
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
        LogConversionIfNeeded(sourceFormat,
                              AVCOL_RANGE_UNSPECIFIED,
                              AVCOL_SPC_UNSPECIFIED,
                              frame->width,
                              frame->height,
                              false,
                              false,
                              false);
        return {};
    }

    const SwsColorProfile sourceProfile = ResolveSourceColorProfile(frame.get(), sourceFormat);

    AVFramePtr convertedFrame = MakeAVFrame();
    if (!convertedFrame) {
        LogConversionIfNeeded(sourceFormat,
                              sourceProfile.range,
                              sourceProfile.colorSpace,
                              frame->width,
                              frame->height,
                              false,
                              sourceProfile.usedFallback,
                              sourceProfile.usedBt2020Fallback);
        return {};
    }

    convertedFrame->format = kDisplayPixelFormat;
    convertedFrame->width = frame->width;
    convertedFrame->height = frame->height;

    int ret = av_frame_get_buffer(convertedFrame.get(), 32);
    if (ret < 0) {
        LogConversionIfNeeded(sourceFormat,
                              sourceProfile.range,
                              sourceProfile.colorSpace,
                              frame->width,
                              frame->height,
                              false,
                              sourceProfile.usedFallback,
                              sourceProfile.usedBt2020Fallback);
        return {};
    }

    av_frame_copy_props(convertedFrame.get(), frame.get());
    if (convertedFrame->time_base.den <= 0) {
        convertedFrame->time_base = frame->time_base;
    }
    convertedFrame->color_range = kDisplayColorRange;
    convertedFrame->colorspace = kDisplayColorSpace;
    convertedFrame->color_primaries = kDisplayColorPrimaries;
    convertedFrame->color_trc = kDisplayColorTransfer;

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
        LogConversionIfNeeded(sourceFormat,
                              sourceProfile.range,
                              sourceProfile.colorSpace,
                              frame->width,
                              frame->height,
                              false,
                              sourceProfile.usedFallback,
                              sourceProfile.usedBt2020Fallback);
        return {};
    }

    ret = sws_setColorspaceDetails(swsContext_,
                                   sws_getCoefficients(sourceProfile.swsColorspace),
                                   RangeFlag(sourceProfile.range),
                                   sws_getCoefficients(kDisplaySwsColorspace),
                                   RangeFlag(kDisplayColorRange),
                                   0,
                                   kFixedPointIdentity,
                                   kFixedPointIdentity);
    if (ret < 0) {
        LogConversionIfNeeded(sourceFormat,
                              sourceProfile.range,
                              sourceProfile.colorSpace,
                              frame->width,
                              frame->height,
                              false,
                              sourceProfile.usedFallback,
                              sourceProfile.usedBt2020Fallback);
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
        LogConversionIfNeeded(sourceFormat,
                              sourceProfile.range,
                              sourceProfile.colorSpace,
                              frame->width,
                              frame->height,
                              false,
                              sourceProfile.usedFallback,
                              sourceProfile.usedBt2020Fallback);
        return {};
    }

    LogConversionIfNeeded(sourceFormat,
                          sourceProfile.range,
                          sourceProfile.colorSpace,
                          frame->width,
                          frame->height,
                          true,
                          sourceProfile.usedFallback,
                          sourceProfile.usedBt2020Fallback);
    return convertedFrame;
}

void VideoFrameConverter::LogConversionIfNeeded(AVPixelFormat sourceFormat,
                                                AVColorRange sourceRange,
                                                AVColorSpace sourceColorSpace,
                                                int width,
                                                int height,
                                                bool success,
                                                bool usedFallback,
                                                bool usedBt2020Fallback)
{
    const ConversionLogSignature signature {
        sourceFormat,
        sourceRange,
        sourceColorSpace,
        width,
        height,
        success,
        usedFallback,
        usedBt2020Fallback
    };

    if (HasLoggedConversionSignature(signature)) {
        return;
    }

    RememberConversionLogSignature(signature);

    if (success) {
        LOG_INFO_STREAM().noquote()
            << "VideoFrameConverter format conversion:"
            << PixelFormatName(sourceFormat)
            << "->" << PixelFormatName(kDisplayPixelFormat)
            << QString("range=%1->%2").arg(ColorRangeName(sourceRange), ColorRangeName(kDisplayColorRange))
            << QString("colorspace=%1->%2").arg(ColorSpaceName(sourceColorSpace), ColorSpaceName(kDisplayColorSpace))
            << QString("size=%1x%2").arg(width).arg(height);
        if (usedBt2020Fallback) {
            LOG_WARN_STREAM().noquote()
                << "VideoFrameConverter BT.2020/HDR full restoration is not supported yet,"
                << "fallback to SDR BT.709 input strategy.";
        } else if (usedFallback) {
            LOG_INFO_STREAM().noquote()
                << "VideoFrameConverter used default SDR color strategy for unspecified frame metadata.";
        }
        return;
    }

    LOG_WARN_STREAM().noquote()
        << "VideoFrameConverter format conversion failed:"
        << PixelFormatName(sourceFormat)
        << "->" << PixelFormatName(kDisplayPixelFormat)
        << QString("range=%1->%2").arg(ColorRangeName(sourceRange), ColorRangeName(kDisplayColorRange))
        << QString("colorspace=%1->%2").arg(ColorSpaceName(sourceColorSpace), ColorSpaceName(kDisplayColorSpace))
        << QString("size=%1x%2").arg(width).arg(height);
}

bool VideoFrameConverter::HasLoggedConversionSignature(const ConversionLogSignature& signature) const
{
    for (const ConversionLogSignature& loggedSignature : loggedConversionSignatures_) {
        if (loggedSignature.sourceFormat == signature.sourceFormat
            && loggedSignature.sourceRange == signature.sourceRange
            && loggedSignature.sourceColorSpace == signature.sourceColorSpace
            && loggedSignature.width == signature.width
            && loggedSignature.height == signature.height
            && loggedSignature.success == signature.success
            && loggedSignature.usedFallback == signature.usedFallback
            && loggedSignature.usedBt2020Fallback == signature.usedBt2020Fallback) {
            return true;
        }
    }

    return false;
}

void VideoFrameConverter::RememberConversionLogSignature(const ConversionLogSignature& signature)
{
    if (loggedConversionSignatures_.size() >= kMaxLoggedConversionSignatures) {
        loggedConversionSignatures_.clear();
    }

    loggedConversionSignatures_.push_back(signature);
}
