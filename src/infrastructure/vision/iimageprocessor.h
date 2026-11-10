#ifndef IIMAGEPROCESSOR_H
#define IIMAGEPROCESSOR_H

#include <QString>

#include <opencv2/core/mat.hpp>

struct AVFrame;

/**
 * @brief IImageProcessor 图像处理器接口
 * @details 只负责对输入图像执行处理，不访问 UI、播放管线、同步器、
 *          视频帧队列或 OpenGL 渲染资源。
 */
class IImageProcessor
{
public:
    virtual ~IImageProcessor() = default;

    /**
     * @brief Name 返回处理器名称
     * @return QString 处理器名称
     */
    virtual QString Name() const = 0;

    /**
     * @brief Enabled 返回处理器是否启用
     * @return true 已启用
     */
    virtual bool Enabled() const = 0;

    /**
     * @brief SetEnabled 设置处理器启用状态
     * @param enabled true 启用，false 禁用
     */
    virtual void SetEnabled(bool enabled) = 0;

    /**
     * @brief Process 处理图像
     * @details 第一版采用原地处理语义，处理器不得保存 image 的数据指针。
     * @param image 待处理图像
     * @return true 处理成功
     */
    virtual bool Process(cv::Mat& image) = 0;

    /**
     * @brief SupportsYuvProcessing 是否支持直接处理 YUV420P 的 Y 平面
     * @details 支持时可以直接跳过 BGR 往返，降低全帧转换成本。
     * @return true 支持 YUV 快速路径
     */
    virtual bool SupportsYuvProcessing() const
    {
        return false;
    }

    /**
     * @brief Process 直接处理 YUV420P 视频帧
     * @param frame 待处理视频帧
     * @return true 处理成功
     */
    virtual bool Process(AVFrame* frame)
    {
        (void)frame;
        return false;
    }
};

#endif // IIMAGEPROCESSOR_H
