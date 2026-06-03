#ifndef GLOBAL_H
#define GLOBAL_H

#include <mutex>
#include <condition_variable>
#include <deque>
#include <string>

#define MAXSIZE 30
#define A_PACKETMAXSIZE 30

struct VideoInfo{
    std::string videoname = "NAN";      ///< 视频文件名
    double FrameRate = 30;              ///< 刷新率(fps)
    double A_timeBase;                  ///< 音频时间基
    double V_timeBase;                  ///< 视频时间基
    int duration = 0;                   ///< 总时长(s)
};

extern struct VideoInfo g_videoinfo;

extern std::condition_variable cv_A_packet_NotEmpty;    ///< 判断音频包队列是否为空，音频解码线程使用
extern std::condition_variable cv_A_packet_NotFull;     ///< 判断音频包队列是否为满，解复用线程使用
extern std::mutex mtx_A_packet;                         ///< 用于保护音频包队列，解复用和音频解码线程出入包时使用

extern std::condition_variable cv_A_frame_NotEmpty;     ///< 判断音频帧队列是否为空，音频播放器使用
extern std::condition_variable cv_A_frame_NotFull;      ///< 判断音频帧队列是否为满，音频解码线程使用
extern std::mutex mtx_A_frame;                          ///< 用于保护音频帧队列，解码线程和音频播放器出入帧时使用

extern std::condition_variable cv_V_packet_NotEmpty;    ///< 判断视频包队列是否为空，视频解码线程使用
extern std::condition_variable cv_V_packet_NotFull;     ///< 判断视频包队列是否为满，解复用线程使用
extern std::mutex mtx_V_packet;                         ///< 用于保护视频包队列，解复用和视频解码线程给出入包时使用

extern std::condition_variable cv_V_frame_NotEmpty;     ///< 判断视频帧队列是否为空，渲染器使用
extern std::condition_variable cv_V_frame_NotFull;      ///< 判断视频帧队列是否为满，视频解码线程使用
extern std::mutex mtx_V_frame;                          ///< 用于保护视频帧队列，视频解码线程和渲染线程使用

extern bool g_pause; ///< 暂停标志
extern bool g_stop; ///< 停止标志

#endif // GLOBAL_H
