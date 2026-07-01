#include "FileMediaSource.h"
#include "tiny_log/Logging.h"

#include <QDir>

FileMediaSource::~FileMediaSource() {
    if (ic) {
        avformat_close_input(&ic);
        ic = nullptr;
    }
}

bool FileMediaSource::Open() {
    if (avformat_open_input(&ic, FileInfo_.filepath_.toUtf8().constData(), nullptr, nullptr)) {
        LOG_WARN("FileMediaSource open failed:", FileInfo_.filepath_);
        return false;
    }

    if (avformat_find_stream_info(ic, nullptr) < 0) {
        LOG_WARN("FileMediaSource stream info lookup failed:", FileInfo_.filepath_);
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
    if (ic) {
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
    return true;
}

bool FileMediaSource::HasDuration() const {
    return true;
}

bool FileMediaSource::IsRealtime() const {
    return false;
}
