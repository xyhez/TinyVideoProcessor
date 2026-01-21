#ifndef GLOBAL_H
#define GLOBAL_H

#include <mutex>
#include <condition_variable>

extern std::condition_variable g_cv_demuxer_audio; ///< 音频解码线程取包之后通过它唤醒解复用线程
extern std::mutex           g_mtx_demuxer_audio;
extern std::condition_variable g_cv_demuxer_video;///< 视频解码线程取包之后通过它唤醒解复用线程
extern std::mutex           g_mtx_demuxer_video;
extern std::condition_variable g_cv_decoder_audio;///< 解复用线程生产音频包后通过它唤醒音频解码线程
extern std::mutex           g_mtx_decoder_audio;
extern std::condition_variable g_cv_decoder_video;///< 解复用线程生产视频包后通过它唤醒视频解码线程
extern std::mutex           g_mtx_decoder_video;
extern std::condition_variable g_cv_audioplayer;///< 音频解码线程生产出原始帧后通过它唤醒音频播放线程
extern std::mutex           g_mtx_audioplayer;
extern std::condition_variable g_cv_videoRender;///< 视频解码线程生产出原始帧后通过它唤醒视频渲染线程
extern std::mutex           g_mtx_videorender;
extern bool                 g_done;///< 暂停标志

#endif // GLOBAL_H
