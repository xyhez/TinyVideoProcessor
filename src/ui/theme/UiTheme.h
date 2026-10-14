#ifndef UI_THEME_UITHEME_H
#define UI_THEME_UITHEME_H

class QApplication;

/**
 * @brief UiTheme 提供应用级 UI 主题入口
 */
class UiTheme final {
public:
    UiTheme() = delete;

    /**
     * @brief Apply 为应用设置统一的深色视频终端主题
     * @param application Qt 应用对象
     */
    static void Apply(QApplication& application);
};

#endif  // UI_THEME_UITHEME_H
