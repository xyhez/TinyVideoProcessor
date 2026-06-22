#include "FileMediaSource.h"

#include <QDir>

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

QString FileMediaSource::SourceIdentity() const {
    return QString("file:%1").arg(QDir::cleanPath(FileInfo_.filepath_));
}

bool FileMediaSource::IsSeekable() const {
    return true; // 文件源支持 seek
}

bool FileMediaSource::HasDuration() const {
    return true; // 文件源有确定的时长
}

bool FileMediaSource::IsRealtime() const {
    return false; // 文件源不是实时源
}

