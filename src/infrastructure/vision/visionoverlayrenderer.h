#ifndef TINYVIDEOPROCESSOR_VISIONOVERLAYRENDERER_H
#define TINYVIDEOPROCESSOR_VISIONOVERLAYRENDERER_H

#include <opencv2/core/mat.hpp>

#include "infrastructure/vision/visiontypes.h"

/**
 * @brief VisionOverlayRenderer 统一识别结果绘制器
 *
 * 把 VisionResultSet 里的检测框、OCR 文本区域以及状态信息叠加到
 * cv::Mat 上。绘制逻辑与具体识别算法解耦：
 * - 具体 pipeline 只输出结构化结果，不直接画到帧上
 * - Overlay 只理解 VisionResultSet，不理解算法细节
 *
 * 第一版只支持 ASCII / Latin-1 字符显示（cv::putText 限制）。
 * 非 Latin-1 字符（含中文）会被降级为 '?'。识别管线应保证 label
 * 和 text 使用英文/数字。
 */
class VisionOverlayRenderer {
public:
    VisionOverlayRenderer() = default;

    /**
     * @brief Draw 把识别结果叠加到 image 上
     * @param image 目标画布，会被修改
     * @param resultSet 识别结果集合
     *
     * 空 resultSet 或空 image 时不做任何修改。
     */
    void Draw(cv::Mat& image, const VisionResultSet& resultSet);
};

#endif // TINYVIDEOPROCESSOR_VISIONOVERLAYRENDERER_H
