#include "FileMediaSource.h"


FileMediaSource::~FileMediaSource() {
    if (ic) {
        avformat_close_input(&ic);
        ic = nullptr;
    }
}

bool FileMediaSource::Open() {
    ///< 打开文件
    if(avformat_open_input(&ic,FileInfo_.filepath_.toUtf8().constData(),nullptr,nullptr)) {
        qDebug()<<"文件打开失败~";
        return false;
    }

    ///< 获取流信息
    if (avformat_find_stream_info(ic, nullptr) < 0) {
        qDebug()<<"无法获取流信息";
        avformat_close_input(&ic);
        ic = nullptr;
        return false;
    }

    return true;
}

bool FileMediaSource::Close() {
    if (ic) {
        avformat_close_input(&ic);
        ic = nullptr;
        return true;
    }
    return false;
}

AVFormatContext* FileMediaSource::GetFormatContect() {
    if(ic) {
        return ic;
    }
    return nullptr;
}

MediaSourceType FileMediaSource::GetType() {
    return MediaSourceType::File;
}

Fileinfo& FileMediaSource::GetSourceInfo()  {
    return FileInfo_;
}

void FileMediaSource::SetInfo(QString filepath) {
    FileInfo_.filepath_ = filepath;
}

