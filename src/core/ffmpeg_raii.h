#pragma once

#include <memory>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
}

/**
 * @brief AVPacketDeleter AVPacket 的 RAII 释放器
 */
struct AVPacketDeleter {
    /**
     * @brief operator() 使用 FFmpeg API 释放 AVPacket
     * @param packet 待释放的 AVPacket 指针
     */
    void operator()(AVPacket* packet) const noexcept {
        if (packet) {
            av_packet_free(&packet);
        }
    }
};

/**
 * @brief AVFrameDeleter AVFrame 的 RAII 释放器
 */
struct AVFrameDeleter {
    /**
     * @brief operator() 使用 FFmpeg API 释放 AVFrame
     * @param frame 待释放的 AVFrame 指针
     */
    void operator()(AVFrame* frame) const noexcept {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

/**
 * @brief AVPacketPtr 表达 AVPacket 单一所有权
 * @details 调用 FFmpeg C API 时使用 .get() 获取裸指针观察视图；
 *          只有在明确把所有权转移给旧接口时才允许使用 .release()。
 */
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

/**
 * @brief AVFramePtr 表达 AVFrame 单一所有权
 * @details 调用 FFmpeg C API 时使用 .get() 获取裸指针观察视图；
 *          只有在明确把所有权转移给 sink 或旧接口时才允许使用 .release()。
 */
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

/**
 * @brief MakeAVPacket 创建由 RAII 管理的 AVPacket
 * @return AVPacketPtr 创建成功时持有 AVPacket，失败时为空
 */
[[nodiscard]] inline AVPacketPtr MakeAVPacket() noexcept {
    return AVPacketPtr(av_packet_alloc());
}

/**
 * @brief MakeAVFrame 创建由 RAII 管理的 AVFrame
 * @return AVFramePtr 创建成功时持有 AVFrame，失败时为空
 */
[[nodiscard]] inline AVFramePtr MakeAVFrame() noexcept {
    return AVFramePtr(av_frame_alloc());
}
