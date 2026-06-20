#pragma once

#ifndef TINYVIDEOPROCESSOR_FILEMEDIASOURCE_H
#define TINYVIDEOPROCESSOR_FILEMEDIASOURCE_H
#include "IMediaSource.h"




class FileMediaSource : public IMediaSource {
public:
    /**
     * @brief FileMediaSource 默认构造函数
     */
    FileMediaSource() = default;

    /**
     * @brief FileMediaSource 使用文件路径构造媒体源
     * @param filepath 本地文件路径
     */
    explicit FileMediaSource(QString filepath){
        FileInfo_.filepath_ = filepath;
    }

    /**
     * @brief FileMediaSource 析构函数，释放文件媒体源资源
     */
    ~FileMediaSource();

    /**
     * @brief Open 打开本地媒体文件
     * @return true 打开成功
     * @return false 打开失败
     */
    bool Open() override;

    /**
     * @brief Close 关闭本地媒体文件
     * @return true 关闭成功
     * @return false 关闭失败
     */
    bool Close() override;

    /**
     * @brief GetFormatContect 获取 FFmpeg 格式上下文
     * @return AVFormatContext 指针
     */
    AVFormatContext* GetFormatContect() override;

    /**
     * @brief GetType 获取媒体源类型
     * @return 文件媒体源类型
     */
    MediaSourceType GetType() override;

    /**
     * @brief GetSourceInfo 获取文件媒体源信息
     * @return 文件媒体源信息引用
     */
    Fileinfo& GetSourceInfo() override;

    /**
     * @brief SetInfo 设置文件路径信息
     * @param filepath 本地文件路径
     */
    void SetInfo(QString filepath) override;

    /**
     * @brief IsSeekable 文件源支持 seek
     * @return true
     */
    bool IsSeekable() const override;

    /**
     * @brief HasDuration 文件源有确定的时长
     * @return true
     */
    bool HasDuration() const override;

    /**
     * @brief IsRealtime 文件源不是实时源
     * @return false
     */
    bool IsRealtime() const override;

private:
    AVFormatContext* ic = nullptr;
    struct Fileinfo FileInfo_;
};


#endif //TINYVIDEOPROCESSOR_FILEMEDIASOURCE_H
