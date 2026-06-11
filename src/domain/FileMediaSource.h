#pragma once

#ifndef TINYVIDEOPROCESSOR_FILEMEDIASOURCE_H
#define TINYVIDEOPROCESSOR_FILEMEDIASOURCE_H
#include "IMediaSource.h"




class FileMediaSource : public IMediaSource {
public:
    FileMediaSource() = default;
    explicit FileMediaSource(QString filepath){
        FileInfo_.filepath_ = filepath;
    }
    ~FileMediaSource();
    bool Open() override;
    bool Close() override;
    AVFormatContext* GetFormatContect() override;
    MediaSourceType GetType() override;
    Fileinfo& GetSourceInfo() override;
    void SetInfo(QString) override;
private:
    AVFormatContext* ic = nullptr;
    struct Fileinfo FileInfo_;
};


#endif //TINYVIDEOPROCESSOR_FILEMEDIASOURCE_H
