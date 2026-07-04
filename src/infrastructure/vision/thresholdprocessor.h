#ifndef THRESHOLDPROCESSOR_H
#define THRESHOLDPROCESSOR_H

#include "iimageprocessor.h"

/**
 * @brief ThresholdProcessor 二值化图像处理器
 * @details 输入和输出都保持 CV_8UC3 BGR 格式，阈值参数由应用层或后续 UI 接入。
 */
class ThresholdProcessor : public IImageProcessor
{
public:
    /**
     * @brief ThresholdProcessor 构造函数
     * @param threshold 二值化阈值，范围 0-255
     */
    explicit ThresholdProcessor(int threshold = 128);

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
     * @brief Process 执行二值化处理
     * @param image 待处理 BGR 图像
     * @return true 处理成功
     */
    bool Process(cv::Mat& image) override;

    /**
     * @brief SetThreshold 设置二值化阈值
     * @param threshold 阈值，范围 0-255
     */
    void SetThreshold(int threshold);

    /**
     * @brief Threshold 获取当前二值化阈值
     * @return int 当前阈值
     */
    int Threshold() const;

private:
    bool enabled_ = true;	///< 是否启用二值化处理
    int threshold_ = 128;	///< 二值化阈值，范围 0-255
};

#endif // THRESHOLDPROCESSOR_H
