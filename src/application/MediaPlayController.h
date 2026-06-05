#ifndef TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
#define TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H


/**
 * @brief
 * MediaPlayController是应用层的协调者，
 * 是MainWindow(UI层)和基础设施层组件(demuxer/decoder/player/render)之间的中间层。
 * MainWindow(UI) -按钮事件 -键盘事件 -显示窗口
 * MediaPlayerController(应用层) -协调所有播放组件 -管理生命周期 -处理业务逻辑
 * 基础设施层 -demuxer/decoder/audioplayer/videorender/syncmanager
 */
class MediaPlayController {

};


#endif //TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
