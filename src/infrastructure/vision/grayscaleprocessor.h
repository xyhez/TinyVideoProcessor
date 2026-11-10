#ifndef GRAYSCALEPROCESSOR_H
#define GRAYSCALEPROCESSOR_H

#include "iimageprocessor.h"

#include <atomic>

/**
 * @brief GrayscaleProcessor 灰度化图像处理器
 * @details 输入和输出都保持 CV_8UC3 BGR 格式，避免破坏后续显示链路。
 */
class GrayscaleProcessor : public IImageProcessor
{
public:
    /**
     * @brief GrayscaleProcessor 构造函数
     */
    GrayscaleProcessor() = default;

    /**
     * @brief Name 返回处理器名称
     * @return QString 处理器名称
     */
    QString Name() const override;

    /**
     * @brief Enabled 返回处理器是否启用
     * @return true 已启用
     */
    bool Enabled() const override;

    /**
     * @brief SetEnabled 设置处理器启用状态
     * @param enabled true 启用，false 禁用
     */
    void SetEnabled(bool enabled) override;

    /**
     * @brief Process 执行灰度化处理
     * @param image 待处理 BGR 图像
     * @return true 处理成功
     */
    bool Process(cv::Mat& image) override;

    /**
     * @brief SupportsYuvProcessing 支持直接处理 YUV Y 平面
     * @return true
     */
    bool SupportsYuvProcessing() const override;

    /**
     * @brief Process 直接处理 YUV420P 帧
     * @param frame 待处理视频帧
     * @return true 处理成功
     */
    bool Process(AVFrame* frame) override;

private:
    std::atomic<bool> enabled_{true};	///< 是否启用灰度化处理
};

#endif // GRAYSCALEPROCESSOR_H
