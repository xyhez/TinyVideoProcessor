
#include "global.h"

std::condition_variable cv_A_packet_NotEmpty;    ///< 判断音频包队列是否为空，音频解码线程使用
std::condition_variable cv_A_packet_NotFull;     ///< 判断音频包队列是否为满，解复用线程使用
std::mutex mtx_A_packet;                         ///< 用于保护音频包队列，解复用和音频解码线程出入包时使用

std::condition_variable cv_A_frame_NotEmpty;     ///< 判断音频帧队列是否为空，音频播放器使用
std::condition_variable cv_A_frame_NotFull;      ///< 判断音频帧队列是否为满，音频解码线程使用
std::mutex mtx_A_frame;                          ///< 用于保护音频帧队列，解码线程和音频播放器出入帧时使用

std::condition_variable cv_V_packet_NotEmpty;    ///< 判断视频包队列是否为空，视频解码线程使用
std::condition_variable cv_V_packet_NotFull;     ///< 判断视频包队列是否为满，解复用线程使用
std::mutex mtx_V_packet;                         ///< 用于保护视频包队列，解复用和视频解码线程给出入包时使用

std::condition_variable cv_V_frame_NotEmpty;     ///< 判断视频帧队列是否为空，渲染器使用
std::condition_variable cv_V_frame_NotFull;      ///< 判断视频帧队列是否为满，视频解码线程使用
std::mutex mtx_V_frame;                          ///< 用于保护视频帧队列，视频解码线程和渲染线程使用

bool g_pause = false; ///< 暂停标志
bool g_stop = false; ///< 停止标志
struct VideoInfo g_videoinfo; ///< 文件信息
