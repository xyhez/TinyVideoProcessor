#ifndef EDGEDETECTIONPROCESSOR_H
#define EDGEDETECTIONPROCESSOR_H

#include "iimageprocessor.h"

/**
 * @brief EdgeDetectionProcessor 边缘检测图像处理器
 * @details 第一版使用固定 Canny 参数，输入和输出都保持 CV_8UC3 BGR 格式。
 */
class EdgeDetectionProcessor : public IImageProcessor
{
public:
    /**
     * @brief EdgeDetectionProcessor 构造函数
     */
    EdgeDetectionProcessor() = default;

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
     * @brief Process 执行边缘检测
     * @param image 待处理 BGR 图像
     * @return true 处理成功
     */
    bool Process(cv::Mat& image) override;

private:
    bool enabled_ = true;	///< 是否启用边缘检测处理
};

#endif // EDGEDETECTIONPROCESSOR_H
