#ifndef GLOBAL_H
#define GLOBAL_H

#include <string>

struct VideoInfo{
    std::string videoname = "NAN";      ///< 视频文件名
    double FrameRate = 30;              ///< 刷新率(fps)
    double A_timeBase;                  ///< 音频时间基
    double V_timeBase;                  ///< 视频时间基
    int duration = 0;                   ///< 总时长(s)
};

extern struct VideoInfo g_videoinfo;

#endif // GLOBAL_H
