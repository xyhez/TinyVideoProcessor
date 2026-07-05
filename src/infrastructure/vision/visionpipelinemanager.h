#ifndef TINYVIDEOPROCESSOR_VISIONPIPELINEMANAGER_H
#define TINYVIDEOPROCESSOR_VISIONPIPELINEMANAGER_H

#include <map>
#include <memory>

#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include "infrastructure/vision/ivisionpipeline.h"
#include "infrastructure/vision/visiontypes.h"

/**
 * @brief VisionPipelineManager 视觉分析管线的注册表与单激活切换器
 *
 * Manager 负责三件事：
 * 1. 注册表：启动时把所有可用的 IVisionPipeline 注册进来，dormant 状态。
 * 2. 单激活切换：同一时刻最多一条 pipeline 处于 started 状态。
 *    SetActivePipeline 自动 Shutdown 旧的、Startup 新的。
 * 3. 执行入口：VisionProcessingFrameSink 每帧调用 RunActive，
 *    Manager 转发给当前 active pipeline 的 Run。
 *
 * Manager 不做单例，由 MultiStreamManager 通过 unique_ptr 持有。
 * 生命周期结束时遍历所有 pipeline，仍处于 started 状态的调用 Shutdown。
 */
class VisionPipelineManager : public QObject {
    Q_OBJECT
public:
    /**
     * @brief VisionPipelineManager 构造函数
     * @param parent Qt 父对象
     */
    explicit VisionPipelineManager(QObject* parent = nullptr);

    /**
     * @brief VisionPipelineManager 析构函数，清理仍处于 started 状态的 pipeline
     */
    ~VisionPipelineManager() override;

    VisionPipelineManager(const VisionPipelineManager&) = delete;
    VisionPipelineManager& operator=(const VisionPipelineManager&) = delete;

    /**
     * @brief RegisterPipeline 注册一条识别管线
     * @param pipeline 待注册的 pipeline，所有权转移给 Manager
     *
     * 注册顺序保留，供 AvailablePipelineIds 返回。
     * 空指针或空 id 或重复 id 被静默丢弃并记录警告日志。
     */
    void RegisterPipeline(std::unique_ptr<IVisionPipeline> pipeline);

    /**
     * @brief AvailablePipelineIds 已注册的管线 id 列表
     * @return 按注册顺序返回
     */
    QList<QString> AvailablePipelineIds() const;

    /**
     * @brief FindPipeline 按 id 查找管线
     * @param id 目标管线 id
     * @return pipeline 指针，找不到返回 nullptr；Manager 保留所有权
     */
    IVisionPipeline* FindPipeline(const QString& id) const;

    /**
     * @brief SetActivePipeline 切换当前激活管线
     * @param id 目标管线 id，空字符串表示关闭视觉分析
     * @return true 切换成功；false 目标 id 不存在或 Startup 失败
     *
     * 行为：
     * - id 与当前 active 相同，直接返回 true（幂等）。
     * - id 为空，Shutdown 当前 active，activeId 置空，发 ActivePipelineChanged。
     * - id 不存在，返回 false，保持原状态。
     * - id 存在，先 Shutdown 旧 active，再 Startup 新 target；
     *   Startup 失败时保持无 active 状态，发 PipelineStartupFailed 和
     *   ActivePipelineChanged 通知。
     */
    bool SetActivePipeline(const QString& id);

    /**
     * @brief ActivePipelineId 当前激活的管线 id
     * @return 空字符串表示无激活
     */
    QString ActivePipelineId() const;

    /**
     * @brief ActivePipeline 当前激活的管线指针
     * @return 无激活时返回 nullptr
     */
    IVisionPipeline* ActivePipeline() const;

    /**
     * @brief RunActive 转发一帧给当前 active pipeline
     * @param input 输入帧
     * @param out 结果集合
     * @return true 处理成功；false 无 active 或处理失败
     */
    bool RunActive(const VisionFrame& input, VisionResultSet& out);

signals:
    /**
     * @brief ActivePipelineChanged 激活管线变化
     * @param id 新激活 id，空字符串表示关闭
     */
    void ActivePipelineChanged(QString id);

    /**
     * @brief PipelineStartupFailed 激活时 Startup 失败
     * @param id 目标管线 id
     * @param error 错误描述
     */
    void PipelineStartupFailed(QString id, QString error);

private:
    std::map<QString, std::unique_ptr<IVisionPipeline>> pipelines_;	///< 注册表，按 id 索引
    QVector<QString> registrationOrder_;	///< 注册顺序，保持 UI 展示稳定
    QString activeId_;	///< 当前激活的管线 id，空字符串表示无激活
};

#endif // TINYVIDEOPROCESSOR_VISIONPIPELINEMANAGER_H
