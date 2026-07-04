#ifndef IMAGEPROCESSORPIPELINE_H
#define IMAGEPROCESSORPIPELINE_H

#include "iimageprocessor.h"

#include <memory>
#include <vector>

/**
 * @brief ImageProcessorPipeline 图像处理器链
 * @details 管理一组 IImageProcessor，并按添加顺序执行已启用处理器。
 *          不接入播放链路，不访问 VFrame_、SyncManager 或 UI。
 */
class ImageProcessorPipeline
{
public:
    /**
     * @brief ImageProcessorPipeline 构造函数
     */
    ImageProcessorPipeline() = default;

    /**
     * @brief AddProcessor 添加图像处理器
     * @param processor 图像处理器
     * @return true 添加成功
     */
    bool AddProcessor(std::shared_ptr<IImageProcessor> processor);

    /**
     * @brief ClearProcessors 清空所有图像处理器
     */
    void ClearProcessors();

    /**
     * @brief Process 按顺序执行已启用处理器
     * @param image 待处理图像
     * @return true 处理成功
     */
    bool Process(cv::Mat& image);

    /**
     * @brief ProcessorCount 返回处理器数量
     * @return int 处理器数量
     */
    int ProcessorCount() const;

    /**
     * @brief LastProcessElapsedMs 返回最近一次处理耗时
     * @return double 最近一次处理耗时，单位毫秒
     */
    double LastProcessElapsedMs() const;

private:
    std::vector<std::shared_ptr<IImageProcessor>> processors_;	///< 图像处理器列表
    double lastProcessElapsedMs_ = 0.0;	///< 最近一次处理耗时，单位毫秒
};

#endif // IMAGEPROCESSORPIPELINE_H
