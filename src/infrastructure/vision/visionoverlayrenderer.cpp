#include "visionoverlayrenderer.h"

#include <algorithm>
#include <string>

#include <QByteArray>

#include <opencv2/imgproc.hpp>

namespace {

const cv::Scalar kColorDetection(0, 255, 0);     ///< 检测框颜色（BGR 亮绿）
const cv::Scalar kColorTextRegion(255, 128, 0);  ///< OCR 文本框颜色（BGR 亮蓝）
const cv::Scalar kColorLabelBg(0, 255, 0);       ///< 标签背景（与检测框同色）
const cv::Scalar kColorLabelFg(0, 0, 0);         ///< 标签文字（黑）
const cv::Scalar kColorStatus(0, 255, 255);      ///< 状态栏文字（BGR 黄）
const cv::Scalar kColorStatusBg(0, 0, 0);        ///< 状态栏背景（黑）
const cv::Scalar kColorError(0, 0, 255);         ///< 错误提示文字（BGR 红）

constexpr int kFontFace = cv::FONT_HERSHEY_SIMPLEX;
constexpr double kFontScale = 0.5;
constexpr int kFontThickness = 1;
constexpr int kBoxThickness = 2;
constexpr int kLabelPaddingX = 4;
constexpr int kLabelPaddingY = 2;
constexpr int kStatusMarginX = 8;
constexpr int kStatusMarginY = 8;
constexpr int kStatusLineGap = 4;

/**
 * @brief ToAscii 把 QString 转成 Latin-1 std::string,非 Latin-1 字符降级为 '?'
 *
 * cv::putText 不支持 UTF-8 多字节字符,直接传 UTF-8 会显示成乱码。
 * 用 toLatin1() 保证输出可预测:ASCII 保持原样,中文等变成 '?'。
 */
std::string ToAscii(const QString& text)
{
    const QByteArray bytes = text.toLatin1();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

/**
 * @brief ClampToImage 把 QRectF 转成 cv::Rect 并裁剪到 image 边界
 * @return 无效或完全在图像外时返回 width <= 0 的 rect
 */
cv::Rect ClampToImage(const QRectF& rect, const cv::Mat& image)
{
    if (image.empty() || !rect.isValid()) {
        return cv::Rect(0, 0, 0, 0);
    }

    int x = static_cast<int>(std::floor(rect.left()));
    int y = static_cast<int>(std::floor(rect.top()));
    int w = static_cast<int>(std::ceil(rect.width()));
    int h = static_cast<int>(std::ceil(rect.height()));

    const int imageW = image.cols;
    const int imageH = image.rows;

    if (x >= imageW || y >= imageH || x + w <= 0 || y + h <= 0) {
        return cv::Rect(0, 0, 0, 0);
    }

    x = std::max(x, 0);
    y = std::max(y, 0);
    w = std::min(w, imageW - x);
    h = std::min(h, imageH - y);

    return cv::Rect(x, y, w, h);
}

/**
 * @brief DrawLabelWithBackground 在 anchor 位置绘制带背景的文字标签
 * @param image 目标画布
 * @param text 文字内容(ASCII)
 * @param anchor 标签左下角期望位置
 * @param bgColor 背景色
 * @param fgColor 前景色
 */
void DrawLabelWithBackground(cv::Mat& image,
                             const std::string& text,
                             const cv::Point& anchor,
                             const cv::Scalar& bgColor,
                             const cv::Scalar& fgColor)
{
    if (text.empty()) {
        return;
    }

    int baseline = 0;
    const cv::Size textSize = cv::getTextSize(text, kFontFace, kFontScale, kFontThickness, &baseline);

    const int bgW = textSize.width + kLabelPaddingX * 2;
    const int bgH = textSize.height + baseline + kLabelPaddingY * 2;

    cv::Point bgTopLeft(anchor.x, anchor.y - textSize.height - baseline - kLabelPaddingY);
    // 若标签会超出图像顶部,回落到 anchor 下方
    if (bgTopLeft.y < 0) {
        bgTopLeft.y = anchor.y + kLabelPaddingY;
    }

    // 水平不越界
    bgTopLeft.x = std::clamp(bgTopLeft.x, 0, std::max(0, image.cols - bgW));
    // 竖直不越界
    bgTopLeft.y = std::clamp(bgTopLeft.y, 0, std::max(0, image.rows - bgH));

    const cv::Rect bgRect(bgTopLeft.x, bgTopLeft.y, bgW, bgH);
    cv::rectangle(image, bgRect, bgColor, cv::FILLED);

    const cv::Point textOrigin(bgTopLeft.x + kLabelPaddingX,
                               bgTopLeft.y + kLabelPaddingY + textSize.height);
    cv::putText(image, text, textOrigin, kFontFace, kFontScale, fgColor, kFontThickness, cv::LINE_AA);
}

/**
 * @brief DrawDetectionBoxes 绘制单条 VisionResult 里的所有检测框
 */
void DrawDetectionBoxes(cv::Mat& image, const VisionResult& result)
{
    for (const DetectionBox& box : result.boxes) {
        const cv::Rect rect = ClampToImage(box.box, image);
        if (rect.width <= 0 || rect.height <= 0) {
            continue;
        }

        cv::rectangle(image, rect, kColorDetection, kBoxThickness);

        QString labelText;
        if (!box.label.isEmpty()) {
            labelText = box.label;
        } else if (box.classId >= 0) {
            labelText = QString("id:%1").arg(box.classId);
        }
        if (box.confidence > 0.0F) {
            const QString conf = QString::number(box.confidence * 100.0, 'f', 1) + "%";
            labelText = labelText.isEmpty() ? conf : labelText + " " + conf;
        }
        if (labelText.isEmpty()) {
            continue;
        }

        const cv::Point anchor(rect.x, rect.y);
        DrawLabelWithBackground(image, ToAscii(labelText), anchor, kColorLabelBg, kColorLabelFg);
    }
}

/**
 * @brief DrawTextRegions 绘制单条 VisionResult 里的所有 OCR 文本区域
 */
void DrawTextRegions(cv::Mat& image, const VisionResult& result)
{
    for (const TextRegion& region : result.texts) {
        const cv::Rect rect = ClampToImage(region.box, image);
        if (rect.width <= 0 || rect.height <= 0) {
            continue;
        }

        cv::rectangle(image, rect, kColorTextRegion, kBoxThickness);

        if (region.text.isEmpty()) {
            continue;
        }

        QString displayText = region.text;
        if (region.confidence > 0.0F) {
            displayText += QString(" (%1%)").arg(region.confidence * 100.0, 0, 'f', 1);
        }

        // OCR 文本标签画在框上方,避免遮挡框内文字
        const cv::Point anchor(rect.x, rect.y);
        DrawLabelWithBackground(image, ToAscii(displayText), anchor, kColorTextRegion, kColorLabelFg);
    }
}

/**
 * @brief DrawStatusBar 在画面左上角绘制每条 VisionResult 的耗时和数量摘要
 */
void DrawStatusBar(cv::Mat& image, const VisionResultSet& resultSet)
{
    if (resultSet.results.isEmpty()) {
        return;
    }

    int lineY = kStatusMarginY;

    for (const VisionResult& result : resultSet.results) {
        QString status = QString("[%1] boxes:%2 texts:%3 %4ms")
                             .arg(result.pipelineId.isEmpty() ? QString("pipeline") : result.pipelineId)
                             .arg(result.boxes.size())
                             .arg(result.texts.size())
                             .arg(result.elapsedMs, 0, 'f', 2);

        const std::string statusAscii = ToAscii(status);
        int baseline = 0;
        const cv::Size textSize = cv::getTextSize(statusAscii, kFontFace, kFontScale, kFontThickness, &baseline);

        const int bgW = textSize.width + kLabelPaddingX * 2;
        const int bgH = textSize.height + baseline + kLabelPaddingY * 2;
        const cv::Rect bgRect(kStatusMarginX, lineY, bgW, bgH);
        if (bgRect.x + bgRect.width > image.cols || bgRect.y + bgRect.height > image.rows) {
            break;	// 状态栏溢出画面就停止绘制,避免越界
        }
        cv::rectangle(image, bgRect, kColorStatusBg, cv::FILLED);

        const cv::Point textOrigin(bgRect.x + kLabelPaddingX,
                                   bgRect.y + kLabelPaddingY + textSize.height);
        cv::putText(image, statusAscii, textOrigin, kFontFace, kFontScale,
                    kColorStatus, kFontThickness, cv::LINE_AA);

        lineY += bgH + kStatusLineGap;

        if (!result.errorMessage.isEmpty()) {
            const QString errLine = QString("[%1] error: %2")
                                       .arg(result.pipelineId.isEmpty() ? QString("pipeline") : result.pipelineId)
                                       .arg(result.errorMessage);
            const std::string errAscii = ToAscii(errLine);
            int errBaseline = 0;
            const cv::Size errSize = cv::getTextSize(errAscii, kFontFace, kFontScale, kFontThickness, &errBaseline);
            const int errBgW = errSize.width + kLabelPaddingX * 2;
            const int errBgH = errSize.height + errBaseline + kLabelPaddingY * 2;
            const cv::Rect errBgRect(kStatusMarginX, lineY, errBgW, errBgH);
            if (errBgRect.x + errBgRect.width > image.cols || errBgRect.y + errBgRect.height > image.rows) {
                break;
            }
            cv::rectangle(image, errBgRect, kColorStatusBg, cv::FILLED);
            const cv::Point errOrigin(errBgRect.x + kLabelPaddingX,
                                      errBgRect.y + kLabelPaddingY + errSize.height);
            cv::putText(image, errAscii, errOrigin, kFontFace, kFontScale,
                        kColorError, kFontThickness, cv::LINE_AA);
            lineY += errBgH + kStatusLineGap;
        }
    }
}

} // namespace

void VisionOverlayRenderer::Draw(cv::Mat& image, const VisionResultSet& resultSet)
{
    if (image.empty() || resultSet.results.isEmpty()) {
        return;
    }

    for (const VisionResult& result : resultSet.results) {
        DrawDetectionBoxes(image, result);
        DrawTextRegions(image, result);
    }

    DrawStatusBar(image, resultSet);
}
